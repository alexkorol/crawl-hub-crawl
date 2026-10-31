/**
 * @file
 * @brief crawl-hub temple teller.
**/

#include "AppHdr.h"

#include "hub-bank.h"
#include "hub-relics.h"

#include <chrono>

#include "branch.h"
#include "coordit.h"
#include "env.h"
#include "files.h"
#include "json.h"
#include "json-wrapper.h"
#include "libutil.h"
#include "macro.h"
#include "message.h"
#include "mon-util.h"
#include "player.h"
#include "prompt.h"
#include "state.h"
#include "stringutil.h"
#ifdef USE_TILE_WEB
#include "tileweb.h"
#endif

string hub_bank_format_op(const hub_bank_op &op)
{
    return make_stringf("%s %d %s", op.kind.c_str(), op.amount,
                        op.op_id.c_str());
}

bool hub_bank_parse_op(const string &text, hub_bank_op &op)
{
    const vector<string> parts = split_string(" ", text, false, false);
    if (parts.size() != 3)
        return false;
    if (parts[0] != "deposit" && parts[0] != "withdraw")
        return false;
    if (parts[1].empty() || parts[1].size() > 10
        || parts[1].find_first_not_of("0123456789") != string::npos)
    {
        return false;
    }
    const long long amount = atoll(parts[1].c_str());
    if (amount < 1 || amount > HUB_BANK_MAX_AMOUNT)
        return false;
    if (parts[2].empty() || parts[2].size() > HUB_BANK_MAX_OP_ID)
        return false;
    op.kind = parts[0];
    op.amount = static_cast<int>(amount);
    op.op_id = parts[2];
    return true;
}

string hub_bank_make_op_id(const string &name, int64_t birth, uint64_t seed,
                           int seq)
{
    // Character names may hold spaces and punctuation; keep the id a token.
    string clean;
    for (char c : name)
        if (isaalnum(c) || c == '_')
            clean += c;
    if (clean.empty())
        clean = "char";
    const string tail = make_stringf("-%lld-%llx-%d", (long long)birth,
                                     (unsigned long long)seed, seq);
    const size_t room = HUB_BANK_MAX_OP_ID - strlen(HUB_BANK_REFUND_SUFFIX);
    if (clean.size() + tail.size() > room)
        clean.resize(room - tail.size());
    return clean + tail;
}

string hub_bank_refund_op_id(const string &op_id)
{
    const string id = op_id + HUB_BANK_REFUND_SUFFIX;
    // Ids from older builds may use the whole limit; the tail is the unique
    // part, so drop the head.
    return id.size() > HUB_BANK_MAX_OP_ID
           ? id.substr(id.size() - HUB_BANK_MAX_OP_ID) : id;
}

hub_bank_result hub_bank_classify(const string &msg, const string &reply_op_id,
                                  bool final, const string &op_id)
{
    if (op_id.empty() || reply_op_id != op_id)
        return hub_bank_result::unrelated;
    if (msg == "hub_bank_ok")
        return hub_bank_result::ok;
    if (msg == "hub_bank_fail")
        return final ? hub_bank_result::refused : hub_bank_result::unknown;
    return hub_bank_result::unrelated;
}

void hub_place_fixed_teller()
{
    if (!player_in_branch(BRANCH_TEMPLE))
        return;
    coord_def kiosk;
    for (rectangle_iterator ri(1); ri; ++ri)
    {
        if (env.grid(*ri) == DNGN_HUB_TELLER)
            return; // already there (level regenerated with one)
        if (env.grid(*ri) == DNGN_HUB_KIOSK && kiosk.origin())
            kiosk = *ri;
    }

    // Beside the kiosk when there is room (fixed order, no RNG).
    if (!kiosk.origin())
    {
        for (adjacent_iterator ai(kiosk); ai; ++ai)
        {
            if (in_bounds(*ai) && env.grid(*ai) == DNGN_FLOOR
                && !monster_at(*ai) && env.igrid(*ai) == NON_ITEM)
            {
                env.grid(*ai) = DNGN_HUB_TELLER;
                return;
            }
        }
    }
    coord_def p;
    if (hub_free_floor_cell(p, true) || hub_free_floor_cell(p, false))
        env.grid(p) = DNGN_HUB_TELLER;
}

#ifdef USE_TILE_WEB

// Longer than the bridge's own timeout to Node (3 s), so its "unreachable"
// answer normally arrives before we give up.
static const int HUB_BANK_TIMEOUT_MS = 4000;

static string _json_string(const JsonNode *obj, const char *key)
{
    const JsonNode *n = json_find_member(obj, key);
    return n && n->tag == JSON_STRING ? n->string_ : "";
}

static double _json_number(const JsonNode *obj, const char *key, double def)
{
    const JsonNode *n = json_find_member(obj, key);
    return n && n->tag == JSON_NUMBER ? n->number_ : def;
}

static bool _json_bool(const JsonNode *obj, const char *key)
{
    const JsonNode *n = json_find_member(obj, key);
    return n && n->tag == JSON_BOOL && n->bool_;
}

static void _send(const JsonWrapper &j)
{
    tiles.write_message("*");
    tiles.write_message("%s", j.to_string().c_str());
    tiles.finish_message();
}

/// Wait for a reply that `accept` takes, skipping late answers to earlier
/// requests. Returns the reply (caller owns it in a JsonWrapper) or nullptr
/// on timeout.
template<typename F>
static JsonNode *_wait_for(F accept)
{
    const auto deadline = chrono::steady_clock::now()
                          + chrono::milliseconds(HUB_BANK_TIMEOUT_MS);
    while (true)
    {
        const auto left = chrono::duration_cast<chrono::milliseconds>(
                              deadline - chrono::steady_clock::now()).count();
        if (left <= 0)
            return nullptr;
        const string text = tiles.hub_wait_reply((int)left);
        if (text.empty())
            return nullptr;
        JsonNode *reply = json_decode(text.c_str());
        if (reply && reply->tag == JSON_OBJECT && accept(reply))
            return reply;
        if (reply)
            json_delete(reply);
    }
}

/// Save so the recorded op (and the gold taken for it) cannot be undone by a
/// crash after the request leaves. False when checkpoints are disabled.
static bool _checkpoint()
{
    if (crawl_state.disables[DIS_SAVE_CHECKPOINTS])
        return false;
    save_level(level_id::current());
    save_game(false);
    return true;
}

static int _next_seq()
{
    const int seq = you.props.exists(HUB_BANK_SEQ_KEY)
                    ? you.props[HUB_BANK_SEQ_KEY].get_int() + 1 : 1;
    you.props[HUB_BANK_SEQ_KEY] = seq;
    return seq;
}

static string _next_op_id()
{
    return hub_bank_make_op_id(you.your_name, (int64_t)you.birth_time,
                               crawl_state.seed, _next_seq());
}

/// Send one op and classify the answer. `balance` and `reason` are filled
/// from the reply when there is one.
static hub_bank_result _send_op(const char *msg, const string &op_id,
                                int amount, const string &to, int &balance,
                                string &reason)
{
    JsonWrapper j(json_mkobject());
    hub_common_fields(j.node, msg);
    json_append_member(j.node, "amount", json_mknumber(amount));
    json_append_member(j.node, "opId", json_mkstring(op_id));
    if (!to.empty())
        json_append_member(j.node, "to", json_mkstring(to));
    _send(j);

    hub_bank_result result = hub_bank_result::unknown;
    JsonWrapper reply(_wait_for([&](const JsonNode *r)
    {
        result = hub_bank_classify(_json_string(r, "msg"),
                                   _json_string(r, "opId"),
                                   _json_bool(r, "final"), op_id);
        return result != hub_bank_result::unrelated;
    }));
    if (!reply.node)
    {
        reason = "the hub did not answer";
        return hub_bank_result::unknown;
    }
    balance = (int)_json_number(reply.node, "balance", -1);
    reason = _json_string(reply.node, "reason");
    return result;
}

/// Send the pending op (already recorded and saved) and settle it. Returns
/// true when it is settled (confirmed or refused), false when still unknown.
static bool _settle(const hub_bank_op &op)
{
    int balance = -1;
    string reason;
    const hub_bank_result r = _send_op(("hub_bank_" + op.kind).c_str(),
                                       op.op_id, op.amount, "", balance,
                                       reason);
    const bool deposit = op.kind == "deposit";
    switch (r)
    {
    case hub_bank_result::ok:
        if (!deposit)
            you.add_gold(op.amount);
        you.props.erase(HUB_BANK_PENDING_KEY);
        mprf("The teller %s %d gold piece%s.%s",
             deposit ? "banks your" : "counts out", op.amount,
             op.amount == 1 ? "" : "s",
             balance >= 0 ? make_stringf(" Your account holds %d.",
                                         balance).c_str() : "");
        break;
    case hub_bank_result::refused:
        if (deposit)
            you.add_gold(op.amount);
        you.props.erase(HUB_BANK_PENDING_KEY);
        mprf("The teller declines the %s%s%s%s", op.kind.c_str(),
             reason.empty() ? "" : ": ", reason.c_str(),
             deposit ? " and hands your gold back." : ".");
        break;
    default:
        mprf("The teller can't confirm the %s of %d gold (%s); it will be "
             "settled on your next visit.", op.kind.c_str(), op.amount,
             reason.empty() ? "no answer" : reason.c_str());
        return false;
    }
    // Not needed for correctness (a repeat opId is answered the same way),
    // but saves a re-send after a crash.
    _checkpoint();
    return true;
}

static int _ask_amount(const char *what, int max_amount)
{
    const int amount = prompt_for_int(
        make_stringf("%s how much (1-%d)? ", what, max_amount).c_str(), true);
    if (amount <= 0)
    {
        canned_msg(MSG_OK);
        return 0;
    }
    if (amount > max_amount)
    {
        mprf("That's more than %d gold.", max_amount);
        return 0;
    }
    return amount;
}

/// Deposit or withdraw: record, save, send, settle.
static void _gold_op(const char *kind, int amount)
{
    hub_bank_op op;
    op.kind = kind;
    op.amount = amount;
    op.op_id = _next_op_id();
    if (op.kind == "deposit")
        you.del_gold(amount);
    you.props[HUB_BANK_PENDING_KEY] = hub_bank_format_op(op);
    if (!_checkpoint())
        dprf("hub teller: checkpoints disabled, %s not saved first", kind);
    _settle(op);
}

static bool _valid_hub_name(const string &name)
{
    if (name.size() < 3 || name.size() > 20)
        return false;
    for (char c : name)
        if (!isaalnum(c))
            return false;
    return true;
}

static void _transfer(int balance)
{
    char buf[40];
    if (msgwin_get_line("Send gold to which hub player? ", buf, sizeof(buf))
        || !*buf)
    {
        canned_msg(MSG_OK);
        return;
    }
    const string to = trimmed_string(buf);
    if (!_valid_hub_name(to))
    {
        mpr("Hub player names are 3 to 20 letters and digits.");
        return;
    }
    const int amount = _ask_amount("Send", balance > 0 ? min(balance,
                                   HUB_BANK_MAX_AMOUNT) : HUB_BANK_MAX_AMOUNT);
    if (!amount)
        return;
    if (!yesno(make_stringf("Send %d gold from your bank to %s?", amount,
                            to.c_str()).c_str(), true, 'n'))
    {
        canned_msg(MSG_OK);
        return;
    }
    // The sequence number must be saved before the id goes out, or a crash
    // could hand the same opId to a later op (and its stored answer).
    const string op_id = _next_op_id();
    _checkpoint();
    int new_balance = -1;
    string reason;
    switch (_send_op("hub_bank_transfer", op_id, amount, to, new_balance,
                     reason))
    {
    case hub_bank_result::ok:
        mprf("The teller sends %d gold piece%s to %s.%s", amount,
             amount == 1 ? "" : "s", to.c_str(),
             new_balance >= 0 ? make_stringf(" Your account holds %d.",
                                             new_balance).c_str() : "");
        break;
    case hub_bank_result::refused:
        mprf("The teller declines the transfer%s%s.",
             reason.empty() ? "" : ": ", reason.c_str());
        break;
    default:
        mprf("The teller can't confirm the transfer (%s); check your "
             "balance on your next visit.",
             reason.empty() ? "no answer" : reason.c_str());
        break;
    }
}
#endif

void hub_teller()
{
#ifndef USE_TILE_WEB
    mpr("The teller's window is shuttered. (The hub is only reachable "
        "through webtiles.)");
#else
    if (!tiles.has_receivers())
    {
        mpr("The teller's window is shuttered. (The hub is only reachable "
            "through webtiles.)");
        return;
    }

    // Settle an op whose answer was lost before anything else.
    if (you.props.exists(HUB_BANK_PENDING_KEY))
    {
        hub_bank_op op;
        const string text = you.props[HUB_BANK_PENDING_KEY].get_string();
        if (!hub_bank_parse_op(text, op))
        {
            mprf(MSGCH_ERROR, "The teller can't read your old slip (%s).",
                 text.c_str());
            you.props.erase(HUB_BANK_PENDING_KEY);
        }
        else
        {
            mprf("The teller looks up your unfinished %s of %d gold.",
                 op.kind.c_str(), op.amount);
            if (!_settle(op))
                return;
        }
    }

    JsonWrapper open(json_mkobject());
    hub_common_fields(open.node, "hub_bank_open");
    _send(open);
    JsonWrapper reply(_wait_for([](const JsonNode *r)
    {
        const string msg = _json_string(r, "msg");
        // A fail carrying an opId is a late answer to an op, not to us.
        return msg == "hub_bank_state"
               || msg == "hub_bank_fail" && _json_string(r, "opId").empty();
    }));
    if (!reply.node || _json_string(reply.node, "msg") != "hub_bank_state")
    {
        const string reason = reply.node ? _json_string(reply.node, "reason")
                                         : "the hub did not answer";
        mprf("The teller is away%s%s.", reason.empty() ? "" : ": ",
             reason.c_str());
        return;
    }
    const int balance = (int)_json_number(reply.node, "balance", 0);

    mprf(MSGCH_PROMPT, "Teller: you carry %d gold, your account holds %d. "
         "(d)eposit, (w)ithdraw, (t)ransfer, or Esc.", you.gold, balance);
    const int key = getchm();
    clear_messages();
    if (key == 'd' || key == 'D')
    {
        if (you.gold <= 0)
        {
            mpr("You have no gold to deposit.");
            return;
        }
        if (const int amount = _ask_amount("Deposit",
                                           min(you.gold, HUB_BANK_MAX_AMOUNT)))
        {
            _gold_op("deposit", amount);
        }
    }
    else if (key == 'w' || key == 'W')
    {
        if (balance <= 0)
        {
            mpr("Your account is empty.");
            return;
        }
        // Keep you.gold well inside an int.
        const int room = HUB_BANK_MAX_AMOUNT - min(you.gold,
                                                   HUB_BANK_MAX_AMOUNT);
        if (const int amount = _ask_amount("Withdraw", min(balance, room)))
            _gold_op("withdraw", amount);
    }
    else if (key == 't' || key == 'T')
        _transfer(balance);
    else
        canned_msg(MSG_OK);
#endif
}

void hub_bank_flush_pending()
{
#ifdef USE_TILE_WEB
    if (!tiles.has_receivers() || !you.props.exists(HUB_BANK_PENDING_KEY))
        return;
    hub_bank_op op;
    if (!hub_bank_parse_op(you.props[HUB_BANK_PENDING_KEY].get_string(), op))
        return;
    if (op.kind == "deposit")
    {
        JsonWrapper j(json_mkobject());
        hub_common_fields(j.node, "hub_bank_deposit");
        json_append_member(j.node, "amount", json_mknumber(op.amount));
        json_append_member(j.node, "opId", json_mkstring(op.op_id));
        _send(j);
        return;
    }

    // A withdrawal the hub may have applied: the gold left the bank but the
    // character never got it. Ask again (a repeat opId returns the stored
    // result, and a first arrival applies it now); if it happened, pay it
    // back. Both ids are idempotent on the hub.
    int balance = -1;
    string reason;
    const hub_bank_result r = _send_op("hub_bank_withdraw", op.op_id,
                                       op.amount, "", balance, reason);
    if (r != hub_bank_result::ok)
    {
        dprf("hub teller: pending withdraw %s at game end: %s",
             op.op_id.c_str(), r == hub_bank_result::refused ? "refused"
                                                             : reason.c_str());
        return;
    }
    const string refund = hub_bank_refund_op_id(op.op_id);
    const hub_bank_result rr = _send_op("hub_bank_deposit", refund, op.amount,
                                        "", balance, reason);
    dprf("hub teller: refund %s of %d gold: %s", refund.c_str(), op.amount,
         rr == hub_bank_result::ok ? "ok" : reason.c_str());
#endif
}
