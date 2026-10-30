/**
 * @file
 * @brief crawl-hub: sell inventory items to dungeon shops.
**/

#include "AppHdr.h"

#include "hub-shop.h"
#include "hub-relics.h"
#include "hub-stash.h"

#include "invent.h"
#include "item-name.h"
#include "items.h"
#include "message.h"
#include "notes.h"
#include "output.h"
#include "player.h"
#include "prompt.h"
#include "shopping.h"
#include "stringutil.h"

bool hub_shop_buys_class(shop_type type, object_class_type cls)
{
    switch (type)
    {
    case SHOP_WEAPON:
    case SHOP_WEAPON_ANTIQUE:
        return cls == OBJ_WEAPONS || cls == OBJ_MISSILES;
    case SHOP_ARMOUR:
    case SHOP_ARMOUR_ANTIQUE:
        return cls == OBJ_ARMOUR;
    case SHOP_JEWELLERY:
        return cls == OBJ_JEWELLERY || cls == OBJ_TALISMANS;
    case SHOP_BOOK:
        return cls == OBJ_BOOKS;
    case SHOP_DISTILLERY:
        return cls == OBJ_POTIONS;
    case SHOP_SCROLL:
        return cls == OBJ_SCROLLS || cls == OBJ_BOOKS;
    case SHOP_GENERAL:
    case SHOP_GENERAL_ANTIQUE:
        return true;
#if TAG_MAJOR_VERSION == 34
    case SHOP_EVOKABLES: // legacy saves only
        return cls == OBJ_WANDS || cls == OBJ_MISCELLANY;
#endif
    default:
        return false;
    }
}

string hub_shop_refusal(const item_def &item, bool equipped)
{
    string why;
    if (!hub_item_is_stashable(item, &why))
        return why;
    if (item.base_type == OBJ_GEMS)
        return "gems are trophies, not merchandise";
    if (item.props.exists(HUB_RELIC_KEY))
        return "this relic belongs to the lost and found of the hub";
    if (equipped)
        return "take it off first";
    return "";
}

int hub_shop_sell_price(const item_def &item, const shop_struct &shop)
{
    // What the player knows: unidentified items price as DCSS prices them
    // unidentified, which is lower.
    const long long buy = hub_shop_item_value(item, shop.greed,
                                              item.is_identified());
    const long long price = buy * HUB_SELL_FRACTION_NUM
                            / HUB_SELL_FRACTION_DEN;
    return static_cast<int>(max(1LL, price));
}

void hub_shop_sell(shop_struct &shop)
{
    const string shopname = shop_name(shop);
    vector<const item_def*> items;
    for (const item_def &it : you.inv)
        if (it.defined() && hub_shop_buys_class(shop.type, it.base_type))
            items.push_back(&it);
    if (items.empty())
    {
        mprf("%s doesn't buy anything you carry.", shopname.c_str());
        more();
        return;
    }

    vector<SelItem> selected = select_items(items, "Sell what?", false,
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

        const string why = hub_shop_refusal(*sel.item,
                                            item_is_equipped(*sel.item)
                                            || item_is_melded(*sel.item));
        if (!why.empty())
        {
            mprf("The shopkeeper won't buy %s: %s.",
                 copy.name(DESC_THE).c_str(), why.c_str());
            continue;
        }

        const int price = hub_shop_sell_price(copy, shop);
        const string prompt = make_stringf("Sell %s for %d gold piece%s?",
                                           copy.name(DESC_THE).c_str(), price,
                                           price == 1 ? "" : "s");
        if (!yesno(prompt.c_str(), true, 'n'))
        {
            canned_msg(MSG_OK);
            continue;
        }

        take_note(Note(NOTE_MESSAGE, 0, 0,
                       make_stringf("Sold %s for %d gold piece%s",
                                    copy.name(DESC_A).c_str(), price,
                                    price == 1 ? "" : "s")));
        dec_inv_item_quantity(slot, copy.quantity);
        you.add_gold(price);
        mprf("You sell %s for %d gold piece%s. You now have %d gold "
             "piece%s.", copy.name(DESC_THE).c_str(), price,
             price == 1 ? "" : "s", you.gold, you.gold == 1 ? "" : "s");
    }
    more(); // the shop menu reopens over the messages
}
