# crawl-hub-crawl

Fork of crawl/crawl at branch `stone_soup-0.34` (upstream remote `upstream`).
Companion Node server: `../crawl-hub`. Plan and decisions: `../dcss-mp-plan/PLAN.md`
(read section 4 Non-goals before touching anything).

Fork-only code lives in `crawl-ref/source/hub/` (C++) and
`crawl-ref/source/webserver/webtiles/hub/` (Python), gated by `HUB=y` (make) and
`hub_enabled` (webserver config). Upstream files get only small, marked call-outs:
`#ifdef HUB` in C++, `if config.get('hub_enabled')` in Python. Never touch
`main.cc` `_input`/`world_reacts`, `mon-act.cc` scheduling, or the turn model.

## Environment
- Python for build and webserver lives in `.venv` (uv). Always prefix commands:
  `export PATH="/opt/homebrew/opt/ccache/libexec:$PWD/.venv/bin:$PATH"` (run from repo root).
- GNU make 3.81 (Apple). WEBTILES=y and TILES=y are mutually exclusive.

## Commands (run from `crawl-ref/source` after the PATH export above)
- Build debug+webtiles with hub features: `make -j8 debug WEBTILES=y HUB=y` (plain `make -j8 debug WEBTILES=y` must keep building: it is upstream)
- Rebuild and show only problems: `make -j8 debug WEBTILES=y 2>&1 | grep -E 'error|warning' | head -50`
- Regenerate `compile_commands.json` (clangd): `bear --output ../../compile_commands.json -- make -j8 debug WEBTILES=y`
- Lua functional tests (debug build, needs `LANG=en_US.UTF-8`): `./crawl -test` ; one test: `./crawl -test <substring>`
- Unit tests: `make catch2-tests HUB=y` (hub tests are `#ifdef HUB`) ; stress suite (slow): `make test`
- Lint C++: `util/checkwhite -n <files>` and `util/unbrace -n <files>` (`-n` = dry run, reports only)
- Headless repro: `./crawl -headless -seed 1 -no-save -wizard -name t -rc test/stress/woken_rest.rc`
- Webserver tests: `cd webserver && pytest` ; lint: `flake8 webtiles`
- Run webtiles locally: `cd crawl-ref/source && python3 webserver/server.py` then http://localhost:8080
  (`autologin` and `no_cache` live in `webserver/config.py`).

- Hub bridge (fork-only, `webserver/webtiles/hub/`): enable with env `HUB_ENABLED=1 HUB_BRIDGE_SECRET=<s>`;
  endpoints `/hub/token` and `/hub/games`, loopback only, header `X-Hub-Secret`. Tests: `pytest webtiles/hub`.

## Architecture pointers (read on demand)
- `docs/ARCHITECTURE.md` (ours): game loop, webtiles IPC, items, shops, features, hooks.
- `docs/HUB-PROTOCOL.md` (ours, shared with ../crawl-hub): every hub message.
- `crawl-ref/docs/develop/`: `coding_conventions.md`, `save_compatibility.txt`, `rng_guidelines.md`, `testing.md`.

## Rules
- Any new persisted field, item prop or tag needs a `TAG_MINOR_VERSION` bump per
  `crawl-ref/docs/develop/save_compatibility.txt`. Item blobs for the stash carry the tag version.
- Hub code never calls game RNG on the seed path (`rng_guidelines.md`). Hub network calls
  during level generation use a short timeout and a no-op fallback.
- Verify crawl symbols with clangd or grep before use; Lua bindings live in `l-*.cc`.
- Commits: 72-char subject, body says why. Generic fixes go upstream as separate commits.
- Never edit `contrib/`, generated `*.o`, `rltiles/` output or `webserver/game_data/static/tileinfo-*.js`.

## Hub game-side pieces (Phase 2a)
- `hub/hub-stash.{h,cc}`: kiosk menu, `hub_item_is_stashable`, `hub_item_to_blob`/`hub_blob_to_item`
  (marshallItem, tag versions travel beside the blob), base64. Spec: `../crawl-hub/docs/SPEC-stash-v0.md`.
- `DNGN_HUB_KIOSK` (last entry of the 34 region in `dungeon-feature-type.h`), data in `feature-data.h`,
  `>` works via `feat_stair_direction`, tile = general shop, entry branch in `main.cc` `_can_take_stairs`.
- Vaults in `dat/des/hub/`; `dat/dlua/loadmaps.lua` skips that directory unless `crawl.hub_enabled()` exists
  (dlua binding in `l-crawl.cc`, HUB only). Lua test: `./crawl -test hub-kiosk`.
- Python: `*hub_*` messages reach `webtiles/hub/bridge.py`, which POSTs them to Node (`hub_node_url`) and sends the reply back as a control message.
- `tiles.hub_wait_reply(ms)` (tileweb.cc, HUB only) blocks for a `hub_*` control message; the kiosk uses it for every request.
- `JsonWrapper` deletes its node in every destructor: never return or copy one by value; pass by reference or hand over a raw `JsonNode*`.
- Gate hub socket features on `tiles.has_receivers()`, not `is_controlled_from_web()` (false on some resumed games).

## Gotchas
- `./crawl -test` needs a debug build and `builddb` (`make test` does both).
- `make catch2-tests` recompiles every object with coverage flags and deletes `./crawl`; run
  `make -j8 debug WEBTILES=y` again afterwards (about 1 minute with ccache). Do not run the two concurrently.
- `-headless -no-save -seed N` gives deterministic reproductions.
- macOS has no `timeout` command; use `gtimeout` (coreutils) or run long jobs in the background.
- Local webtiles creates `webserver/passwd.db3`, `rcs/`, `saves/` on first run; they are untracked scratch state.
- Upstream Makefile bug fixed in this fork: `status-icon-sizes` rule path (commit 575e3b2b05).

## Compaction
Keep: files touched, failing test names, current HUB protocol message list, open TAG bumps.
