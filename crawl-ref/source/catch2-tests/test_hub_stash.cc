#ifdef HUB
#include "catch_amalgamated.hpp"

#include "AppHdr.h"

#include "artefact.h"
#include "art-enum.h"
#include "hub-stash.h"
#include "item-prop.h"
#include "items.h"
#include "orb-type.h"
#include "tag-version.h"

#include "test_player_fixture.h"

static item_def _make(object_class_type base, int sub, int quantity = 1,
                      int plus = 0)
{
    item_def it;
    it.base_type = base;
    it.sub_type = sub;
    it.quantity = quantity;
    it.plus = plus;
    it.rnd = 1; // items with rnd == 0 fail unmarshallItem's validity check
    return it;
}

TEST_CASE_METHOD(MockPlayerYouTestsFixture, "hub blob round trip",
                 "[single-file]")
{
    SECTION("a scroll stack")
    {
        item_def scrolls = _make(OBJ_SCROLLS, SCR_TELEPORTATION, 5);
        scrolls.inscription = "keep";
        item_def back;
        REQUIRE(hub_blob_to_item(hub_item_to_blob(scrolls),
                                 TAG_MINOR_VERSION, back));
        REQUIRE(back.base_type == OBJ_SCROLLS);
        REQUIRE(back.sub_type == SCR_TELEPORTATION);
        REQUIRE(back.quantity == 5);
        REQUIRE(back.inscription == "keep");
    }

    SECTION("a branded weapon")
    {
        item_def axe = _make(OBJ_WEAPONS, WPN_WAR_AXE, 1, 3);
        set_item_ego_type(axe, OBJ_WEAPONS, SPWPN_FLAMING);
        item_def back;
        REQUIRE(hub_blob_to_item(hub_item_to_blob(axe), TAG_MINOR_VERSION,
                                 back));
        REQUIRE(back.base_type == OBJ_WEAPONS);
        REQUIRE(back.sub_type == WPN_WAR_AXE);
        REQUIRE(back.plus == 3);
        REQUIRE(get_weapon_brand(back) == SPWPN_FLAMING);
    }

    SECTION("a randart ring keeps its properties")
    {
        item_def ring = _make(OBJ_JEWELLERY, RING_PROTECTION);
        REQUIRE(make_item_randart(ring));
        artefact_set_property(ring, ARTP_FIRE, 2);
        artefact_set_property(ring, ARTP_STEALTH, 1);
        item_def back;
        REQUIRE(hub_blob_to_item(hub_item_to_blob(ring), TAG_MINOR_VERSION,
                                 back));
        REQUIRE(is_random_artefact(back));
        REQUIRE(artefact_property(back, ARTP_FIRE) == 2);
        REQUIRE(artefact_property(back, ARTP_STEALTH) == 1);
    }

    SECTION("a truncated blob is rejected")
    {
        vector<unsigned char> blob =
            hub_item_to_blob(_make(OBJ_POTIONS, POT_CURING, 2));
        blob.resize(blob.size() / 2);
        item_def back;
        REQUIRE_FALSE(hub_blob_to_item(blob, TAG_MINOR_VERSION, back));
        REQUIRE_FALSE(hub_blob_to_item({}, TAG_MINOR_VERSION, back));
        REQUIRE_FALSE(hub_blob_to_item(hub_item_to_blob(_make(OBJ_POTIONS,
                                                              POT_CURING)),
                                       TAG_MINOR_VERSION + 1, back));
    }
}

TEST_CASE("hub base64 round trip", "[single-file]")
{
    for (size_t len = 0; len < 8; ++len)
    {
        vector<unsigned char> data;
        for (size_t i = 0; i < len; ++i)
            data.push_back(static_cast<unsigned char>(i * 37 + 200));
        vector<unsigned char> back;
        const string text = hub_base64_encode(data);
        REQUIRE(text.size() % 4 == 0);
        REQUIRE(hub_base64_decode(text, back));
        REQUIRE(back == data);
    }
    REQUIRE(hub_base64_encode({'M', 'a', 'n'}) == "TWFu");
    REQUIRE(hub_base64_encode({'M', 'a'}) == "TWE=");
    vector<unsigned char> junk;
    REQUIRE_FALSE(hub_base64_decode("TWF", junk));
    REQUIRE_FALSE(hub_base64_decode("TW!u", junk));
}

TEST_CASE_METHOD(MockPlayerYouTestsFixture, "hub stash exclusions",
                 "[single-file]")
{
    string why;
    REQUIRE_FALSE(hub_item_is_stashable(_make(OBJ_RUNES, RUNE_SNAKE), &why));
    REQUIRE_FALSE(why.empty());
    REQUIRE_FALSE(hub_item_is_stashable(_make(OBJ_ORBS, ORB_ZOT), &why));

    item_def unrand = _make(OBJ_WEAPONS, WPN_LONG_SWORD);
    REQUIRE(make_item_unrandart(unrand, UNRAND_SINGING_SWORD));
    REQUIRE_FALSE(hub_item_is_stashable(unrand, &why));

    REQUIRE(hub_item_is_stashable(_make(OBJ_POTIONS, POT_CURING), &why));
    REQUIRE(why.empty());
}
#endif
