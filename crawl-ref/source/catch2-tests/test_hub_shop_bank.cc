#ifdef HUB
#include "catch_amalgamated.hpp"

#include "AppHdr.h"

#include <set>

#include "artefact.h"
#include "art-enum.h"
#include "hub-bank.h"
#include "hub-relics.h"
#include "hub-shop.h"
#include "item-prop.h"
#include "item-status-flag-type.h"
#include "items.h"
#include "orb-type.h"
#include "shopping.h"

#include "test_player_fixture.h"

static item_def _make(object_class_type base, int sub, int quantity = 1,
                      int plus = 0)
{
    item_def it;
    it.base_type = base;
    it.sub_type = sub;
    it.quantity = quantity;
    it.plus = plus;
    it.rnd = 1;
    return it;
}

static shop_struct _shop(shop_type type, int greed = 15)
{
    shop_struct s;
    s.type = type;
    s.greed = greed;
    s.level = 1;
    return s;
}

TEST_CASE_METHOD(MockPlayerYouTestsFixture, "hub sell price is a third",
                 "[single-file]")
{
    const shop_struct general = _shop(SHOP_GENERAL, 20);
    vector<item_def> items = {
        _make(OBJ_WEAPONS, WPN_LONG_SWORD, 1, 3),
        _make(OBJ_ARMOUR, ARM_PLATE_ARMOUR, 1, 2),
        _make(OBJ_MISSILES, MI_ARROW, 7),
        _make(OBJ_POTIONS, POT_EXPERIENCE),
    };
    items[0].flags |= ISFLAG_IDENTIFIED;
    items[1].flags |= ISFLAG_IDENTIFIED;
    items[3].flags |= ISFLAG_IDENTIFIED;
    for (const item_def &it : items)
    {
        const int buy = hub_shop_item_value(it, general.greed,
                                            it.is_identified());
        const int sell = hub_shop_sell_price(it, general);
        REQUIRE(sell == max(1, buy * HUB_SELL_FRACTION_NUM
                               / HUB_SELL_FRACTION_DEN));
        REQUIRE(sell >= 1);
        REQUIRE(sell < buy);
    }
    // The identified shop price formula is what the shop itself charges.
    REQUIRE(hub_shop_item_value(items[0], general.greed, true)
            == item_price(items[0], general));
    REQUIRE(HUB_SELL_FRACTION_NUM * 3 == HUB_SELL_FRACTION_DEN);

    // Never below one gold piece.
    shop_struct cheap = _shop(SHOP_GENERAL, 1);
    REQUIRE(hub_shop_sell_price(_make(OBJ_MISSILES, MI_STONE, 1), cheap) == 1);
}

TEST_CASE_METHOD(MockPlayerYouTestsFixture,
                 "hub unidentified items sell for less", "[single-file]")
{
    const shop_struct general = _shop(SHOP_GENERAL, 15);
    for (item_def it : { _make(OBJ_POTIONS, POT_EXPERIENCE),
                         _make(OBJ_SCROLLS, SCR_ACQUIREMENT),
                         _make(OBJ_JEWELLERY, RING_WILLPOWER) })
    {
        REQUIRE_FALSE(it.is_identified());
        const int unknown = hub_shop_sell_price(it, general);
        it.flags |= ISFLAG_IDENTIFIED;
        const int known = hub_shop_sell_price(it, general);
        REQUIRE(unknown < known);
    }
}

TEST_CASE("hub shops buy what they stock", "[single-file]")
{
    REQUIRE(hub_shop_buys_class(SHOP_WEAPON, OBJ_WEAPONS));
    REQUIRE(hub_shop_buys_class(SHOP_WEAPON, OBJ_MISSILES));
    REQUIRE(hub_shop_buys_class(SHOP_WEAPON_ANTIQUE, OBJ_MISSILES));
    REQUIRE_FALSE(hub_shop_buys_class(SHOP_WEAPON, OBJ_ARMOUR));
    REQUIRE(hub_shop_buys_class(SHOP_ARMOUR, OBJ_ARMOUR));
    REQUIRE(hub_shop_buys_class(SHOP_ARMOUR_ANTIQUE, OBJ_ARMOUR));
    REQUIRE_FALSE(hub_shop_buys_class(SHOP_ARMOUR, OBJ_WEAPONS));
    REQUIRE(hub_shop_buys_class(SHOP_JEWELLERY, OBJ_JEWELLERY));
    REQUIRE(hub_shop_buys_class(SHOP_JEWELLERY, OBJ_TALISMANS));
    REQUIRE_FALSE(hub_shop_buys_class(SHOP_JEWELLERY, OBJ_POTIONS));
    REQUIRE(hub_shop_buys_class(SHOP_BOOK, OBJ_BOOKS));
    REQUIRE_FALSE(hub_shop_buys_class(SHOP_BOOK, OBJ_SCROLLS));
    REQUIRE(hub_shop_buys_class(SHOP_DISTILLERY, OBJ_POTIONS));
    REQUIRE_FALSE(hub_shop_buys_class(SHOP_DISTILLERY, OBJ_SCROLLS));
    REQUIRE(hub_shop_buys_class(SHOP_SCROLL, OBJ_SCROLLS));
    REQUIRE(hub_shop_buys_class(SHOP_SCROLL, OBJ_BOOKS));
    REQUIRE_FALSE(hub_shop_buys_class(SHOP_SCROLL, OBJ_POTIONS));
    for (auto cls : { OBJ_WEAPONS, OBJ_ARMOUR, OBJ_WANDS, OBJ_STAVES,
                      OBJ_MISCELLANY, OBJ_POTIONS })
    {
        REQUIRE(hub_shop_buys_class(SHOP_GENERAL, cls));
        REQUIRE(hub_shop_buys_class(SHOP_GENERAL_ANTIQUE, cls));
    }
}

TEST_CASE_METHOD(MockPlayerYouTestsFixture, "hub shops refuse some items",
                 "[single-file]")
{
    REQUIRE_FALSE(hub_shop_refusal(_make(OBJ_RUNES, RUNE_SNAKE), false)
                  .empty());
    REQUIRE_FALSE(hub_shop_refusal(_make(OBJ_ORBS, ORB_ZOT), false).empty());
    REQUIRE_FALSE(hub_shop_refusal(_make(OBJ_GEMS, 0), false).empty());

    item_def unrand = _make(OBJ_WEAPONS, WPN_LONG_SWORD);
    REQUIRE(make_item_unrandart(unrand, UNRAND_SINGING_SWORD));
    REQUIRE_FALSE(hub_shop_refusal(unrand, false).empty());

    item_def relic = _make(OBJ_JEWELLERY, RING_PROTECTION);
    REQUIRE(make_item_randart(relic));
    REQUIRE(hub_shop_refusal(relic, false).empty()); // a plain randart sells
    relic.props[HUB_RELIC_KEY] = 42;
    REQUIRE_FALSE(hub_shop_refusal(relic, false).empty());

    const item_def sword = _make(OBJ_WEAPONS, WPN_LONG_SWORD);
    REQUIRE(hub_shop_refusal(sword, false).empty());
    REQUIRE(hub_shop_refusal(sword, true) == "take it off first");
}

TEST_CASE("hub bank pending op round trip", "[single-file]")
{
    hub_bank_op op;
    op.kind = "deposit";
    op.amount = 1234;
    op.op_id = "Bob-1700000000-abc-3";
    const string text = hub_bank_format_op(op);
    REQUIRE(text == "deposit 1234 Bob-1700000000-abc-3");
    hub_bank_op back;
    REQUIRE(hub_bank_parse_op(text, back));
    REQUIRE(back.kind == op.kind);
    REQUIRE(back.amount == op.amount);
    REQUIRE(back.op_id == op.op_id);

    REQUIRE(hub_bank_parse_op("withdraw 1 x", back));
    REQUIRE(back.kind == "withdraw");
    REQUIRE_FALSE(hub_bank_parse_op("transfer 5 x", back));
    REQUIRE_FALSE(hub_bank_parse_op("deposit 0 x", back));
    REQUIRE_FALSE(hub_bank_parse_op("deposit -5 x", back));
    REQUIRE_FALSE(hub_bank_parse_op("deposit 1000000001 x", back));
    REQUIRE_FALSE(hub_bank_parse_op("deposit 5", back));
    REQUIRE_FALSE(hub_bank_parse_op("deposit 5 x y", back));
    REQUIRE_FALSE(hub_bank_parse_op("deposit 5 " + string(81, 'a'), back));
    REQUIRE_FALSE(hub_bank_parse_op("", back));
}

TEST_CASE("hub bank op ids are unique and short", "[single-file]")
{
    set<string> ids;
    for (int seq = 1; seq <= 50; ++seq)
    {
        const string id = hub_bank_make_op_id("Ser Bob!", 1700000000,
                                              0xdeadbeefcafeULL, seq);
        REQUIRE(id.size() <= HUB_BANK_MAX_OP_ID);
        REQUIRE(id.find(' ') == string::npos);
        ids.insert(id);
    }
    REQUIRE(ids.size() == 50);
    // Another character of the same name differs by birth time and seed.
    REQUIRE(hub_bank_make_op_id("SerBob", 1700000000, 1, 1)
            != hub_bank_make_op_id("SerBob", 1700000001, 1, 1));
    REQUIRE(hub_bank_make_op_id("SerBob", 1700000000, 1, 1)
            != hub_bank_make_op_id("SerBob", 1700000000, 2, 1));
    const string longname = hub_bank_make_op_id(string(200, 'n'), INT64_MAX,
                                                UINT64_MAX, INT32_MAX);
    REQUIRE(longname.size() == HUB_BANK_MAX_OP_ID);
    hub_bank_op op;
    REQUIRE(hub_bank_parse_op("deposit 5 " + longname, op));
}

TEST_CASE("hub bank replies settle only their own op", "[single-file]")
{
    const string id = "Bob-1-2-3";
    REQUIRE(hub_bank_classify("hub_bank_ok", id, false, id)
            == hub_bank_result::ok);
    REQUIRE(hub_bank_classify("hub_bank_fail", id, true, id)
            == hub_bank_result::refused);
    REQUIRE(hub_bank_classify("hub_bank_fail", id, false, id)
            == hub_bank_result::unknown);
    // Late answers to other ops, and replies without an opId, are ignored.
    REQUIRE(hub_bank_classify("hub_bank_ok", "Bob-1-2-2", false, id)
            == hub_bank_result::unrelated);
    REQUIRE(hub_bank_classify("hub_bank_fail", "", true, id)
            == hub_bank_result::unrelated);
    REQUIRE(hub_bank_classify("hub_bank_state", id, false, id)
            == hub_bank_result::unrelated);
    REQUIRE(hub_bank_classify("hub_bank_ok", "", false, "")
            == hub_bank_result::unrelated);
}
#endif
