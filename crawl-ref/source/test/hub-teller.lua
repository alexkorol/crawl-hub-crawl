-- crawl-hub (fork-only): the Temple gets a teller beside its fixed kiosk,
-- and no other fixed stop does. No-op in builds without HUB=y.

if not (crawl.hub_enabled and crawl.hub_enabled()) then
  crawl.message("hub-teller: skipped (not a HUB build)")
  return
end

debug.goto_place("Temple")
for i = 1, 3 do
  crawl.delay(0)
  test.regenerate_level()
  local teller = test.find_feature("hub_teller")
  local kiosk = test.find_feature("hub_kiosk")
  test.map_assert(teller, "No hub_teller placed in the Temple")
  test.map_assert(kiosk, "No hub_kiosk placed in the Temple")
  test.map_assert(math.abs(teller.x - kiosk.x) <= 1
                  and math.abs(teller.y - kiosk.y) <= 1,
                  "hub_teller is not beside the Temple kiosk")
end

for _, place in ipairs({ "D:1", "Vaults:1", "Hell" }) do
  debug.goto_place(place)
  crawl.delay(0)
  test.regenerate_level()
  test.map_assert(not test.find_feature("hub_teller"),
                  "Unexpected hub_teller in " .. place)
end
