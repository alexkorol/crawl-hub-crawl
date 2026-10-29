# webtiles (Python/Tornado)
See `/.claude/rules/webserver.md`. Run from this directory with the repo `.venv` on PATH:
- tests: `pytest` ; lint: `flake8 webtiles` ; server: `cd .. && python3 webserver/server.py`
Hub bridge code belongs in `webtiles/hub/`. Server-only `*` messages are the game->server channel.
