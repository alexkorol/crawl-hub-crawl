------------------------------------------------------------------------------
-- loadmaps.lua:
--
-- Compiles and loads .des files that Crawl needs.
------------------------------------------------------------------------------

local des_files = file.datadir_files_recursive("dat/des", ".des")

for _, file in ipairs(des_files) do
  -- crawl-hub: vaults under des/hub/ use features that exist only in HUB=y
  -- builds, where crawl.hub_enabled is defined.
  if not file:find("^hub/") or (crawl.hub_enabled and crawl.hub_enabled()) then
    dgn.load_des_file("des/" .. file)
  end
end
