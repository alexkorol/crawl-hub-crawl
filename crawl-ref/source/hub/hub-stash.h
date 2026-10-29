/**
 * @file
 * @brief crawl-hub stash kiosk: deposit items to the hub as serialized blobs.
 *
 * Fork-only (built with HUB=y). See docs/HUB-PROTOCOL.md section 3 and
 * crawl-hub/docs/SPEC-stash-v0.md.
**/

#pragma once

#include <string>
#include <vector>

struct item_def;

/// Open the kiosk menu for the player standing on a DNGN_HUB_KIOSK.
void hub_kiosk();

/// After level generation: put a kiosk on a free floor cell of the fixed
/// stops (Temple, Vaults:1, Hell Vestibule). No-op elsewhere.
void hub_place_fixed_kiosk();

/// Hard exclusions (runes, the Orb, unrandarts). `why` gets a short reason.
bool hub_item_is_stashable(const item_def &item, std::string *why = nullptr);

/// marshallItem output for one item, no header. Tag versions travel beside it.
std::vector<unsigned char> hub_item_to_blob(const item_def &item);

/// Inverse of hub_item_to_blob. False (and `item` unspecified) on a bad blob.
bool hub_blob_to_item(const std::vector<unsigned char> &blob, int tag_minor,
                      item_def &item);

std::string hub_base64_encode(const std::vector<unsigned char> &data);
bool hub_base64_decode(const std::string &text, std::vector<unsigned char> &out);
