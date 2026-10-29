---
paths:
  - "crawl-ref/source/webserver/**/*.py"
  - "crawl-ref/source/webserver/static/**/*.js"
  - "crawl-ref/source/webserver/game_data/static/**/*.js"
---
- Tornado 6, Python 3.12 in `.venv`. Tests: `cd crawl-ref/source/webserver && pytest`; lint: `flake8 webtiles`.
- Server-only messages from the game are prefixed `*` and handled in `webtiles/process_handler.py`; they never reach browsers. New hub messages follow this convention and are documented in `docs/HUB-PROTOCOL.md`.
- Before changing `ws_handler.py` or `process_handler.py`, make sure a characterization test with a fake crawl process covers the path you touch.
- Hub-specific Python goes in `webtiles/hub/`; call-outs in upstream files are guarded by `config.get('hub_enabled')`.
