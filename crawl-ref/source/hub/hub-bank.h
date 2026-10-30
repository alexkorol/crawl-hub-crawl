/**
 * @file
 * @brief crawl-hub temple teller: move gold between the character and the
 *        account's hub bank, and bank to bank between accounts.
 *
 * Fork-only (built with HUB=y), a deliberate deviation from vanilla DCSS (owner
 * decision 2026-10-30 in dcss-mp-plan/PLAN.md). Wire messages: HUB-PROTOCOL.md
 * section 3 (`*hub_bank_*`, `hub_bank_state`, `hub_bank_ok`/`hub_bank_fail`).
 *
 * Exactly once: a deposit or withdrawal is recorded in you.props (gold taken
 * first for a deposit) and the game is saved before the request is sent. The
 * hub applies each (account, opId) once and answers a repeat with the stored
 * result, so an op whose answer was lost is re-sent with the same opId at the
 * next teller visit (or, for a deposit, when the game ends).
**/

#pragma once

#include <cstdint>
#include <string>

/// you.props key: the unconfirmed deposit or withdrawal, as
/// hub_bank_format_op() text.
#define HUB_BANK_PENDING_KEY "hub_bank_pending"
/// you.props key: last opId sequence number used by this character.
#define HUB_BANK_SEQ_KEY "hub_bank_seq"

static const int HUB_BANK_MAX_AMOUNT = 1000000000;
static const size_t HUB_BANK_MAX_OP_ID = 80;

struct hub_bank_op
{
    std::string kind;   ///< "deposit" or "withdraw"
    int amount = 0;     ///< 1..HUB_BANK_MAX_AMOUNT
    std::string op_id;  ///< unique per account, <= HUB_BANK_MAX_OP_ID chars
};

/// "<kind> <amount> <opId>".
std::string hub_bank_format_op(const hub_bank_op &op);
/// Inverse of hub_bank_format_op; false on anything malformed.
bool hub_bank_parse_op(const std::string &text, hub_bank_op &op);

/// "<name>-<birth>-<seed hex>-<seq>", name trimmed so the id fits in
/// HUB_BANK_MAX_OP_ID characters.
std::string hub_bank_make_op_id(const std::string &name, int64_t birth,
                                uint64_t seed, int seq);

enum class hub_bank_result
{
    ok,        ///< hub_bank_ok for this opId: the op happened
    refused,   ///< hub_bank_fail, final, for this opId: nothing happened
    unknown,   ///< hub_bank_fail without final, or no answer: keep pending
    unrelated, ///< a reply to something else (e.g. a late answer): ignore
};

/// Classify one reply to the op `op_id` from its msg, opId and final fields.
hub_bank_result hub_bank_classify(const std::string &msg,
                                  const std::string &reply_op_id,
                                  bool final, const std::string &op_id);

/// Open the teller for the player standing on a DNGN_HUB_TELLER.
void hub_teller();

/// After level generation: in the Temple, put a teller beside the kiosk.
void hub_place_fixed_teller();

/// At game end: re-send an unconfirmed deposit (fire-and-forget, same opId)
/// so the gold the character handed over still reaches the bank.
void hub_bank_flush_pending();
