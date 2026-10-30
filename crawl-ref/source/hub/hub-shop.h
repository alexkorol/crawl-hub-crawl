/**
 * @file
 * @brief crawl-hub: sell inventory items to dungeon shops.
 *
 * Fork-only (built with HUB=y), a deliberate deviation from vanilla DCSS (owner
 * decision 2026-10-30 in dcss-mp-plan/PLAN.md). Press '%' in a shop menu.
**/

#pragma once

#include <string>

#include "object-class-type.h"
#include "shop-type.h"

struct item_def;
struct shop_struct;

/// A shop pays this fraction of its own buy price for an item, rounded down,
/// at least 1 gold.
static const int HUB_SELL_FRACTION_NUM = 1;
static const int HUB_SELL_FRACTION_DEN = 3;

/// Does a shop of `type` buy items of class `cls`? Mirrors what the shop
/// stocks (dungeon.cc item_in_shop); general stores buy anything.
bool hub_shop_buys_class(shop_type type, object_class_type cls);

/// Why a shop won't buy `item` at all ("" when it will): runes, gems, the
/// Orb, unrandarts, returned lost relics, and worn or melded items
/// (`equipped`).
std::string hub_shop_refusal(const item_def &item, bool equipped);

/// What `shop` pays for `item` (its whole quantity): the shop's own buy
/// price at what the player knows of the item, times HUB_SELL_FRACTION.
int hub_shop_sell_price(const item_def &item, const shop_struct &shop);

/// Sell flow: pick items, confirm each price, take the gold.
void hub_shop_sell(shop_struct &shop);
