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
- Build debug+webtiles: `make -j8 debug WEBTILES=y`
- Rebuild and show only problems: `make -j8 debug WEBTILES=y 2>&1 | grep -E 'error|warning' | head -50`
- Regenerate `compile_commands.json` (clangd): `bear --output ../../compile_commands.json -- make -j8 debug WEBTILES=y`
- Lua functional tests (debug build): `./crawl -test` ; one test: `./crawl -test <substring>`
- Unit tests: `make catch2-tests` ; stress suite (slow): `make test`
- Lint C++: `util/checkwhite -n <files>` and `util/unbrace -n <files>` (`-n` = dry run, reports only)
- Headless repro: `./crawl -headless -seed 1 -no-save -wizard -name t -rc test/stress/woken_rest.rc`
- Webserver tests: `cd webserver && pytest` ; lint: `flake8 webtiles`
- Run webtiles locally: `cd crawl-ref/source && python3 webserver/server.py` then http://localhost:8080
  (`autologin` and `no_cache` live in `webserver/config.py`).

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
