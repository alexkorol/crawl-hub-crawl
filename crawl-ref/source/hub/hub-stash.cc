/**
 * @file
 * @brief crawl-hub stash kiosk: deposit to and withdraw from the hub stash
 *        as serialized item blobs, over the webtiles socket.
**/

#include "AppHdr.h"

#include "hub-stash.h"

#include "artefact.h"
#include "art-enum.h"
#include "branch.h"
#include "coord.h"
#include "coordit.h"
#include "dungeon.h"
#include "env.h"
#include "mon-place.h"
#include "random.h"
#include "terrain.h"
#include "invent.h"
#include "item-name.h"
#include "item-prop.h"
#include "items.h"
#include "json.h"
#include "json-wrapper.h"
#include "libutil.h"
#include "macro.h"
#include "message.h"
#include "output.h"
#include "place.h"
#include "player.h"
#include "state.h"
#include "tag-version.h"
#include "tags.h"
#include "view.h"
#ifdef USE_TILE_WEB
#include "tileweb.h"
#endif

bool hub_item_is_stashable(const item_def &item, string *why)
{
    string reason;
    if (item.base_type == OBJ_RUNES)
        reason = "runes stay with the one who earned them";
    else if (item.base_type == OBJ_ORBS)
        reason = "the Orb will not suffer being set down here";
    else if (is_unrandom_artefact(item))
        reason = "this artefact refuses to leave the dungeon";
    if (why)
        *why = reason;
    return reason.empty();
}

vector<unsigned char> hub_item_to_blob(const item_def &item)
{
    vector<unsigned char> buf;
    writer w(&buf);
    marshallItem(w, item);
    return buf;
}

bool hub_blob_to_item(const vector<unsigned char> &blob, int tag_minor,
                      item_def &item)
{
    if (blob.empty() || tag_minor < 0 || tag_minor > TAG_MINOR_VERSION)
        return false;
    try
    {
        reader r(blob, tag_minor);
        unmarshallItem(r, item);
        // reader::valid() is "bytes remain": a good blob is consumed exactly.
        return !r.valid() && item.defined();
    }
    catch (const short_read_exception &)
    {
        return false;
    }
}

static const char _b64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

string hub_base64_encode(const vector<unsigned char> &data)
{
    string out;
    out.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    while (i + 2 < data.size())
    {
        const unsigned n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += _b64_alphabet[(n >> 18) & 63];
        out += _b64_alphabet[(n >> 12) & 63];
        out += _b64_alphabet[(n >> 6) & 63];
        out += _b64_alphabet[n & 63];
        i += 3;
    }
    const size_t rest = data.size() - i;
    if (rest == 1)
    {
        const unsigned n = data[i] << 16;
        out += _b64_alphabet[(n >> 18) & 63];
        out += _b64_alphabet[(n >> 12) & 63];
        out += "==";
    }
    else if (rest == 2)
    {
        const unsigned n = (data[i] << 16) | (data[i + 1] << 8);
        out += _b64_alphabet[(n >> 18) & 63];
        out += _b64_alphabet[(n >> 12) & 63];
        out += _b64_alphabet[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

static int _b64_value(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

bool hub_base64_decode(const string &text, vector<unsigned char> &out)
{
    out.clear();
    if (text.size() % 4 != 0)
        return false;
    unsigned acc = 0;
    int bits = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (c == '=')
        {
            // Padding may only appear in the last two positions.
            if (i + 2 < text.size())
                return false;
            continue;
        }
        const int v = _b64_value(c);
        if (v < 0)
            return false;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back((acc >> bits) & 0xff);
        }
    }
    return true;
}

#ifdef USE_TILE_WEB

static const int HUB_REPLY_TIMEOUT_MS = 2500;

/// Send a server-only message and wait for the hub's reply. Returns a parsed
/// object the caller must own in a JsonWrapper, or nullptr on timeout / bad
/// JSON. (JsonWrapper deletes its node in every destructor, so it is never
/// returned by value.)
static JsonNode *_hub_request(const JsonWrapper &req)
{
    tiles.write_message("*");
    tiles.write_message("%s", req.to_string().c_str());
    tiles.finish_message();
    const string text = tiles.hub_wait_reply(HUB_REPLY_TIMEOUT_MS);
    if (text.empty())
        return nullptr;
    JsonNode *reply = json_decode(text.c_str());
    if (reply && reply->tag != JSON_OBJECT)
    {
        json_delete(reply);
        return nullptr;
    }
    return reply;
}

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

/// Fill `j` (which must own a fresh json_mkobject()) with the common fields.
static void _new_request(JsonWrapper &j, const char *msg)
{
    json_append_member(j.node, "msg", json_mkstring(msg));
    json_append_member(j.node, "charName", json_mkstring(you.your_name));
    json_append_member(j.node, "xl", json_mknumber(you.experience_level));
    json_append_member(j.node, "place",
                       json_mkstring(level_id::current().describe()));
    json_append_member(j.node, "tagMajor", json_mknumber(TAG_MAJOR_VERSION));
    json_append_member(j.node, "tagMinor", json_mknumber(TAG_MINOR_VERSION));
}

static bool _deposit_one(const item_def &item)
{
    JsonWrapper j(json_mkobject());
    _new_request(j, "hub_stash_deposit");
    json_append_member(j.node, "blob",
                       json_mkstring(hub_base64_encode(hub_item_to_blob(item))));
    json_append_member(j.node, "name", json_mkstring(item.name(DESC_PLAIN)));
    json_append_member(j.node, "basetype", json_mknumber(item.base_type));
    json_append_member(j.node, "subtype", json_mknumber(item.sub_type));
    json_append_member(j.node, "quantity", json_mknumber(item.quantity));
    json_append_member(j.node, "isRandart",
                       json_mkbool(is_random_artefact(item)));
    json_append_member(j.node, "isUnrand",
                       json_mkbool(is_unrandom_artefact(item)));
    if (is_random_artefact(item))
    {
        JsonNode *props = json_mkobject();
        artefact_properties_t proprt;
        artefact_properties(item, proprt);
        for (int i = 0; i < ART_PROPERTIES; ++i)
            if (proprt[i])
            {
                json_append_member(props, to_string(i).c_str(),
                                   json_mknumber(proprt[i]));
            }
        json_append_member(j.node, "props", props);
    }
    JsonWrapper reply(_hub_request(j));
    if (!reply.node)
    {
        mpr("The clerk fumbles and hands it back: the hub did not answer.");
        return false;
    }
    if (_json_string(reply.node, "msg") != "hub_stash_deposit_ok")
    {
        const string reason = _json_string(reply.node, "reason");
        mprf("The clerk won't take %s%s%s.", item.name(DESC_THE).c_str(),
             reason.empty() ? "" : ": ", reason.c_str());
        return false;
    }
    return true;
}

static void _deposit_flow()
{
    vector<const item_def*> items;
    for (const item_def &it : you.inv)
        if (it.defined())
            items.push_back(&it);
    if (items.empty())
    {
        mpr("You have nothing to deposit.");
        return;
    }

    vector<SelItem> selected = select_items(items, "Deposit what?", false,
                                            menu_type::drop);
    redraw_screen();
    update_screen();
    if (selected.empty())
    {
        canned_msg(MSG_OK);
        return;
    }

    for (const SelItem &sel : selected)
    {
        if (!sel.item || !sel.item->defined())
            continue;
        // Copy first: dec_inv_item_quantity may clear the inventory slot.
        item_def copy = *sel.item;
        const int slot = sel.item->link;
        if (sel.quantity > 0 && sel.quantity < copy.quantity)
            copy.quantity = sel.quantity;

        string why;
        if (!hub_item_is_stashable(copy, &why))
        {
            mprf("You can't deposit %s: %s.",
                 copy.name(DESC_THE).c_str(), why.c_str());
            continue;
        }
        if (!_deposit_one(copy))
            continue;
        mprf("You deposit %s.", copy.name(DESC_THE).c_str());
        dec_inv_item_quantity(slot, copy.quantity);
    }
}

struct hub_stash_entry
{
    int id;
    item_def item;
};

static vector<hub_stash_entry> _parse_stash_list(const JsonNode *reply,
                                                 int &incompatible)
{
    vector<hub_stash_entry> entries;
    incompatible = 0;
    const JsonNode *items = json_find_member(reply, "items");
    if (!items || items->tag != JSON_ARRAY)
        return entries;
    JsonNode *n;
    json_foreach(n, items)
    {
        if (n->tag != JSON_OBJECT)
            continue;
        const int major = (int)_json_number(n, "tagMajor", -1);
        const int minor = (int)_json_number(n, "tagMinor", -1);
        vector<unsigned char> blob;
        hub_stash_entry e;
        e.id = (int)_json_number(n, "id", -1);
        if (major != TAG_MAJOR_VERSION || minor > TAG_MINOR_VERSION
            || !hub_base64_decode(_json_string(n, "blob"), blob)
            || !hub_blob_to_item(blob, minor, e.item))
        {
            incompatible++;
            continue;
        }
        e.item.link = NON_ITEM;
        e.item.pos.reset();
        entries.push_back(e);
    }
    return entries;
}

static void _withdraw_flow(vector<hub_stash_entry> &entries, int incompatible)
{
    if (entries.empty())
    {
        if (incompatible)
        {
            mprf("Your stash holds %d item%s this version of the game can't "
                 "read.", incompatible, incompatible == 1 ? "" : "s");
        }
        else
            mpr("Your stash is empty.");
        return;
    }
    vector<const item_def*> items;
    for (const hub_stash_entry &e : entries)
        items.push_back(&e.item);
    vector<SelItem> selected = select_items(items, "Withdraw what?", false,
                                            menu_type::pickup);
    redraw_screen();
    update_screen();
    if (selected.empty())
    {
        canned_msg(MSG_OK);
        return;
    }
    for (const SelItem &sel : selected)
    {
        const hub_stash_entry *entry = nullptr;
        for (const hub_stash_entry &e : entries)
            if (&e.item == sel.item)
                entry = &e;
        if (!entry)
            continue;

        JsonWrapper j(json_mkobject());
        _new_request(j, "hub_stash_withdraw_req");
        json_append_member(j.node, "stashItemId", json_mknumber(entry->id));
        JsonWrapper reply(_hub_request(j));
        const string msg = reply.node ? _json_string(reply.node, "msg") : "";
        if (msg != "hub_stash_withdraw_ok")
        {
            const string reason = reply.node ? _json_string(reply.node, "reason")
                                             : "the hub did not answer";
            mprf("The clerk can't hand over %s: %s.",
                 entry->item.name(DESC_THE).c_str(), reason.c_str());
            continue;
        }
        item_def copy = entry->item;
        if (!move_item_to_inv(copy))
        {
            // The hub already marked it withdrawn: put it back.
            JsonWrapper undo(json_mkobject());
            _new_request(undo, "hub_stash_withdraw_undo");
            json_append_member(undo.node, "stashItemId",
                               json_mknumber(entry->id));
            json_append_member(undo.node, "reason",
                               json_mkstring("no room in pack"));
            tiles.write_message("*");
            tiles.write_message("%s", undo.to_string().c_str());
            tiles.finish_message();
            mprf("You have no room for %s; the clerk keeps it.",
                 entry->item.name(DESC_THE).c_str());
        }
    }
}
#endif

void hub_kiosk()
{
#ifndef USE_TILE_WEB
    mpr("The kiosk's bell rings, but nobody answers. (The hub is only "
        "reachable through webtiles.)");
#else
    // The webtiles server must be attached: it relays our socket messages.
    if (!tiles.has_receivers())
    {
        mpr("The kiosk's bell rings, but nobody answers. (The hub is only "
            "reachable through webtiles.)");
        return;
    }

    JsonWrapper open(json_mkobject());
    _new_request(open, "hub_kiosk_open");
    JsonWrapper reply(_hub_request(open));
    if (!reply.node || _json_string(reply.node, "msg") != "hub_stash_list")
    {
        const string reason = reply.node ? _json_string(reply.node, "reason")
                                         : "";
        mprf("The clerk is away%s%s.", reason.empty() ? "" : ": ",
             reason.c_str());
        return;
    }
    int incompatible = 0;
    vector<hub_stash_entry> entries = _parse_stash_list(reply.node,
                                                        incompatible);

    mprf(MSGCH_PROMPT, "Stash kiosk: (d)eposit, (w)ithdraw [%d item%s], "
         "or Esc.", (int)entries.size(), entries.size() == 1 ? "" : "s");
    const int key = getchm();
    clear_messages();
    if (key == 'd' || key == 'D')
        _deposit_flow();
    else if (key == 'w' || key == 'W')
        _withdraw_flow(entries, incompatible);
    else
        canned_msg(MSG_OK);
#endif
}

void hub_place_fixed_kiosk()
{
    const bool fixed_stop = player_in_branch(BRANCH_TEMPLE)
                            || player_in_branch(BRANCH_VESTIBULE)
                            || (player_in_branch(BRANCH_VAULTS)
                                && you.depth == 1);
    if (!fixed_stop)
        return;
    for (rectangle_iterator ri(1); ri; ++ri)
        if (env.grid(*ri) == DNGN_HUB_KIOSK)
            return; // already there (level regenerated with one)

    // Prefer plain floor away from vault-marked cells; fall back to any floor.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int tries = 0; tries < 2000; ++tries)
        {
            const coord_def p = random_in_bounds();
            if (env.grid(p) != DNGN_FLOOR || monster_at(p)
                || env.igrid(p) != NON_ITEM)
            {
                continue;
            }
            if (pass == 0 && map_masked(p, MMT_VAULT))
                continue;
            env.grid(p) = DNGN_HUB_KIOSK;
            return;
        }
    }
}
