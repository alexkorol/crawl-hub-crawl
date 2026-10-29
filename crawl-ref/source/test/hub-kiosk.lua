-- crawl-hub (fork-only): every D:1 gets a stash kiosk, and deeper kiosks
-- are at least sometimes placed. No-op in builds without HUB=y.

if not (crawl.hub_enabled and crawl.hub_enabled()) then
  crawl.message("hub-kiosk: skipped (not a HUB build)")
  return
end

local niters = 5

debug.goto_place("D:1")
for i = 1, niters do
  crawl.message("D:1 kiosk test " .. i .. " of " .. niters)
  crawl.delay(0)
  test.regenerate_level()
  test.map_assert(test.find_feature("hub_kiosk"),
                  "No hub_kiosk created on D:1 (hub_kiosk_d1 should be guaranteed)")
end

-- Deeper floors: the minivault is weighted, so look across several levels.
local found = false
debug.goto_place("D:5")
for i = 1, 20 do
  crawl.delay(0)
  test.regenerate_level()
  if test.find_feature("hub_kiosk") then
    found = true
    break
  end
end
test.map_assert(found, "No hub_kiosk placed on D:5 in 20 generations")

-- Fixed kiosks at the thematic stops.
for _, place in ipairs({ "Temple", "Vaults:1", "Hell" }) do
  debug.goto_place(place)
  crawl.delay(0)
  test.regenerate_level()
  test.map_assert(test.find_feature("hub_kiosk"),
                  "No hub_kiosk placed in " .. place)
end
