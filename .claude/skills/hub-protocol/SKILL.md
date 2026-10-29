---
name: hub-protocol
description: How to add or change a hub message end to end (C++ game -> Python webtiles -> Node crawl-hub -> browser). Use whenever a task adds a `*hub_*` message, a stash/relic/trade event, or changes docs/HUB-PROTOCOL.md.
---
# Adding a hub message

1. Define it first in `docs/HUB-PROTOCOL.md` (this repo) and mirror the TypeScript type in
   `../crawl-hub/shared/protocol.ts` or `../crawl-hub/server/bridge/messages.ts`. Name, direction,
   fields, when emitted, failure behaviour.
2. C++ side (`crawl-ref/source/hub/`): build the JSON with the `tiles` writer helpers used in
   `tileweb.cc` (`tiles.json_open_object()`, `json_write_*`, `json_close_object()`, `finish_message()`).
   Server-only messages use a `msg` value starting with `*`. Guard with `#ifdef HUB`.
3. Python side (`webserver/webtiles/hub/`): handle the `*` message where `process_handler.py`
   dispatches server-only messages, forward to crawl-hub over the bridge, and send any reply back to the
   game as a control message (`tileweb.cc` `_handle_control_message`).
4. Node side: handler in `../crawl-hub/server/bridge/`, persistence in the owning module.
5. Tests at every layer touched: Catch2 for serialization, pytest with the fake process for the relay,
   Vitest for the Node handler.
6. Blobs: serialize with `marshallItem` into `vector<unsigned char>`, base64 in JSON, and include
   `tag_major` and `tag_minor`; refuse mismatches on import.
