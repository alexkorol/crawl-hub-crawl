/**
 * @file
 * @brief crawl-hub lost relics.
**/

#include "AppHdr.h"

#include "hub-relics.h"

#include "artefact.h"
#include "art-enum.h"
#include "branch.h"
#include "coordit.h"
#include "dungeon.h"
#include "env.h"
#include "hiscores.h"
#include "hub-stash.h"
#include "item-name.h"
#include "items.h"
#include "json.h"
#include "json-wrapper.h"
#include "kill-method-type.h"
#include "libutil.h"
#include "mon-place.h"
#include "place.h"
#include "player.h"
#include "state.h"
#include "stringutil.h"
#include "tag-version.h"
#include "terrain.h"
#ifdef USE_TILE_WEB
#include "tileweb.h"
#endif

void hub_json_item(JsonNode *obj, const item_def &item)
{
    json_append_member(obj, "blob",
                       json_mkstring(hub_base64_encode(hub_item_to_blob(item))));
    json_append_member(obj, "tagMajor", json_mknumber(TAG_MAJOR_VERSION));
    json_append_member(obj, "tagMinor", json_mknumber(TAG_MINOR_VERSION));
    json_append_member(obj, "name", json_mkstring(item.name(DESC_PLAIN)));
    json_append_member(obj, "basetype", json_mknumber(item.base_type));
    json_append_member(obj, "subtype", json_mknumber(item.sub_type));
    json_append_member(obj, "quantity", json_mknumber(item.quantity));
    json_append_member(obj, "isRandart", json_mkbool(is_random_artefact(item)));
    json_append_member(obj, "isUnrand",
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
        json_append_member(obj, "props", props);
    }
}

bool hub_free_floor_cell(coord_def &out, bool avoid_vaults)
{
    vector<coord_def> cells;
    for (rectangle_iterator ri(1); ri; ++ri)
    {
        const coord_def p = *ri;
        if (env.grid(p) != DNGN_FLOOR || monster_at(p)
            || env.igrid(p) != NON_ITEM)
        {
            continue;
        }
        if (avoid_vaults && map_masked(p, MMT_VAULT))
            continue;
        cells.push_back(p);
    }
    if (cells.empty())
        return false;
    // Spread picks around without touching the game RNG (seed-safe).
    const size_t idx = (static_cast<size_t>(you.where_are_you) * 7919
                        + static_cast<size_t>(you.depth) * 104729
                        + cells.size() * 31) % cells.size();
    out = cells[idx];
    return true;
}

#ifdef USE_TILE_WEB
static void _send(JsonWrapper &j)
{
    tiles.write_message("*");
    tiles.write_message("%s", j.to_string().c_str());
    tiles.finish_message();
}

static void _common_fields(JsonNode *obj, const char *msg)
{
    json_append_member(obj, "msg", json_mkstring(msg));
    json_append_member(obj, "charName", json_mkstring(you.your_name));
    json_append_member(obj, "xl", json_mknumber(you.experience_level));
    json_append_member(obj, "place",
                       json_mkstring(level_id::current().describe()));
    json_append_member(obj, "branch",
                       json_mkstring(branches[you.where_are_you].abbrevname));
    json_append_member(obj, "depth", json_mknumber(you.depth));
}
#endif

void hub_item_event(const char *kind, const item_def &item)
{
#ifdef USE_TILE_WEB
    if (!tiles.has_receivers() || !item.defined() || !is_random_artefact(item))
        return;
    JsonWrapper j(json_mkobject());
    _common_fields(j.node, "hub_item_event");
    json_append_member(j.node, "kind", json_mkstring(kind));
    hub_json_item(j.node, item);
    _send(j);
#else
    UNUSED(kind, item);
#endif
}

void hub_report_death(const scorefile_entry &se)
{
#ifdef USE_TILE_WEB
    if (!tiles.has_receivers())
        return;
    const int dt = se.get_death_type();
    if (dt == KILLED_BY_LEAVING || dt == KILLED_BY_WINNING
        || dt == KILLED_BY_QUITTING)
    {
        return;
    }
    const string cause = se.death_description(scorefile_entry::DDV_ONELINE);

    auto report = [&](const item_def &item, const char *where)
    {
        if (!item.defined() || !is_random_artefact(item))
            return;
        JsonWrapper j(json_mkobject());
        _common_fields(j.node, "hub_relic_lost");
        json_append_member(j.node, "cause", json_mkstring(cause));
        json_append_member(j.node, "where", json_mkstring(where));
        hub_json_item(j.node, item);
        _send(j);
    };
    for (const item_def &item : you.inv)
        report(item, "pack");
    for (const item_def &item : env.item)
        if (item.defined() && in_bounds(item.pos))
            report(item, "floor");
#else
    UNUSED(se);
#endif
}

void hub_place_lost_relic()
{
#ifdef USE_TILE_WEB
    if (!tiles.has_receivers() || crawl_state.game_is_arena())
        return;
    JsonWrapper q(json_mkobject());
    _common_fields(q.node, "hub_relic_query");
    json_append_member(q.node, "tagMajor", json_mknumber(TAG_MAJOR_VERSION));
    json_append_member(q.node, "tagMinor", json_mknumber(TAG_MINOR_VERSION));
    _send(q);
    const string text = tiles.hub_wait_reply(400);
    if (text.empty())
        return;
    JsonWrapper reply(json_decode(text.c_str()));
    if (!reply.node || reply.node->tag != JSON_OBJECT)
        return;
    const JsonNode *msg = json_find_member(reply.node, "msg");
    if (!msg || msg->tag != JSON_STRING
        || string(msg->string_) != "hub_relic_answer")
    {
        return;
    }
    const JsonNode *id = json_find_member(reply.node, "relicId");
    const JsonNode *blob = json_find_member(reply.node, "blob");
    const JsonNode *minor = json_find_member(reply.node, "tagMinor");
    if (!id || id->tag != JSON_NUMBER || !blob || blob->tag != JSON_STRING
        || !minor || minor->tag != JSON_NUMBER)
    {
        return;
    }
    const int relic_id = (int)id->number_;
    auto fail = [&](const char *reason)
    {
        JsonWrapper f(json_mkobject());
        _common_fields(f.node, "hub_relic_place_failed");
        json_append_member(f.node, "relicId", json_mknumber(relic_id));
        json_append_member(f.node, "reason", json_mkstring(reason));
        _send(f);
    };

    vector<unsigned char> bytes;
    item_def item;
    if (!hub_base64_decode(blob->string_, bytes)
        || !hub_blob_to_item(bytes, (int)minor->number_, item))
    {
        fail("blob not readable by this version");
        return;
    }
    coord_def p;
    if (!hub_free_floor_cell(p, true) && !hub_free_floor_cell(p, false))
    {
        fail("no free floor cell");
        return;
    }
    item.link = NON_ITEM;
    item.pos.reset();
    if (copy_item_to_grid(item, p, item.quantity, true, true) == NON_ITEM)
    {
        fail("level is full of items");
        return;
    }
    JsonWrapper ok(json_mkobject());
    _common_fields(ok.node, "hub_relic_placed");
    json_append_member(ok.node, "relicId", json_mknumber(relic_id));
    _send(ok);
#endif
}
