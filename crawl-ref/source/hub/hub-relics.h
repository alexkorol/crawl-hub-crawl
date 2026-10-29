/**
 * @file
 * @brief crawl-hub lost relics: report randarts lost on death, place returned
 *        relics at level generation, and emit randart item events.
 *
 * Fork-only (built with HUB=y). See docs/HUB-PROTOCOL.md section 3.
**/

#pragma once

#include "coord-def.h"

struct item_def;
class scorefile_entry;
struct JsonNode;

/// Fill `obj` with the item fields the hub stores (blob, tag versions, name,
/// types, quantity, randart flags and properties).
void hub_json_item(JsonNode *obj, const item_def &item);

/// Fire-and-forget `*hub_item_event` for random artefacts (pickup, equip,
/// unequip, drop). No-op for other items or without a webtiles receiver.
void hub_item_event(const char *kind, const item_def &item);

/// On a real death: report every random artefact in the pack and on the
/// floor of this level as `*hub_relic_lost`.
void hub_report_death(const scorefile_entry &se);

/// At the end of level generation: ask the hub for a lost relic that may
/// return here and place it. Bounded by a short timeout.
void hub_place_lost_relic();

/// Deterministic (RNG-free) choice of a free floor cell; false when none.
bool hub_free_floor_cell(coord_def &out, bool avoid_vaults);

class monster;
/// Bughouse: the player killed a monster; tell the hub (fire-and-forget).
void hub_monster_killed(const monster &mons);

/// Unsolicited control messages from the hub (e.g. `hub_spawn`). Returns
/// true when handled. Called from the webtiles control-message dispatch.
bool hub_handle_control(const std::string &msgtype, const std::string &json);
