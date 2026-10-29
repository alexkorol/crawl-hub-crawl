# HUB-PROTOCOL v0

Canonical copy: `crawl-hub-crawl/docs/HUB-PROTOCOL.md`. Mirror: `crawl-hub/docs/HUB-PROTOCOL.md`.
Every message between the four parties is listed here before it is implemented.
Status column: `planned` (not built), `stub` (shape exists, no persistence), `done`.

Parties: **Browser** (TS client), **Node** (crawl-hub), **Py** (webtiles Tornado), **Game** (crawl C++).

## 1. Browser <-> Node (WebSocket `/ws`, JSON tagged on `t`)

Types live in `crawl-hub/shared/protocol.ts`. `PROTOCOL_VERSION` is sent in `hello` and `welcome`.

| Message | Dir | Fields | When | Status |
|---|---|---|---|---|
| `hello` | B->N | `v`, `token?` | socket open; token resumes a session | done |
| `welcome` | N->B | `v`, `tick`, `tickHz`, `selfId` | reply to hello | done |
| `register` | B->N | `username`, `password` | sign up; 3-20 `[A-Za-z0-9_]`, pw 8-200 | done |
| `login` | B->N | `username`, `password` | sign in; 5 failures per 10 min then `rate_limited` | done |
| `auth_ok` | N->B | `accountId`, `username`, `token` | success; token lives 30 days | done |
| `auth_err` | N->B | `reason` | failure, rate-limited, session expired | done |
| `snapshot` | N->B | `tick`, `mapId`, `entities[]`, `selfId` | after join; one entity per account, later socket replaces earlier (`error "replaced"`) | done |
| `delta` | N->B | `tick`, `upsert[]`, `remove[]`, `ackSeq?` | each tick with changes, joined clients only | done |
| `move` | B->N | `dir`, `seq` | key press; applied next tick | done |
| `chat` | B->N | `channel` local/global, `text` | send; 1-300 chars, 5 per 5 s; `/block name`, `/unblock name` | done |
| `chat` | N->B | `channel`, `from`, `text`, `tick` | fan-out; local = Chebyshev <= 8; blocked senders dropped | done |
| `profile_req` / `profile` | B<->N | `accountId` / character, XL, place, milestones, watchUrl | avatar click | planned |
| `play` | B->N | `gameId` | door interaction; Node mints webtiles token | planned (Phase 1) |
| `play_ok` | N->B | `webtilesUrl`, `token` | embed webtiles client | planned |
| `stash_list` / `stash` | B<->N | items with blob metadata (never the blob itself) | town kiosk UI | planned (Phase 2a) |
| `offer_create` / `offer_accept` / `offer_cancel` / `offer_update` | B<->N | `offerId`, `itemId`, `toAccountId`, state | trade escrow | planned (Phase 2a) |
| `rumors` | N->B | relic name, props summary, branch, age | tavern board | planned (Phase 2b) |
| `ping` / `pong` | B<->N | `now` / `now`, `serverNow` | RTT | done |
| `block` | B->N | `username`, `on` | same as `/block` command | done |
| `system` | N->B | `text` | server notices (rate limit, command results) | done |
| `error` | N->B | `reason` | any; `replaced`, `town full`, `protocol version mismatch` | done |

## 2. Node <-> Py (local HTTP + WebSocket bridge, loopback only)

| Endpoint / message | Dir | Fields | Purpose | Status |
|---|---|---|---|---|
| `POST /hub/token` | N->Py | `username`, `ttl` | mint a webtiles login token for `token_login` | planned (Phase 1) |
| `GET /hub/games` | N->Py | | live games list (lobby_entry data) for presence | planned (Phase 1) |
| ws `/hub/events` | Py->N | relayed `*hub_*` game messages, plus `game_started`, `game_ended {reason}` | per-game event stream | planned (Phase 1) |
| ws `/hub/events` | N->Py | `to_game {username, control}` | control message to a game (stash contents, relic answer) | planned (Phase 2a) |

Auth between Node and Py: shared secret from env, loopback bind.

## 3. Game <-> Py (existing crawl Unix socket; JSON; `*` prefix = server-only)

New server-only messages emitted by the game (all behind `HUB=y`):

| `msg` | Fields | When | Status |
|---|---|---|---|
| `*hub_kiosk_open` | `charName`, `xl`, `place` | player interacts with a kiosk feature; Py asks Node for stash list | planned (2a) |
| `*hub_stash_deposit` | `blob` (base64 marshallItem), `tagMajor`, `tagMinor`, `itemName`, `basetype`, `subtype`, `isRandart`, `props?` | item removed from inventory at a kiosk | planned (2a) |
| `*hub_stash_withdraw_req` | `stashItemId` | player picks a stash item in the kiosk menu | planned (2a) |
| `*hub_relic_lost` | one per randart: `blob`, `tag*`, `name`, `props`, `branch`, `depth`, `cause` | player death finalized | planned (2b) |
| `*hub_relic_query` | `branch`, `depth` | level generation in hub mode; game waits <= 200 ms | planned (2b) |
| `*hub_relic_placed` | `relicId`, `branch`, `depth` | relic placed by generator | planned (2b) |
| `*hub_item_event` | `kind` pickup/equip/drop/deposit, `name`, `props`, `place` | randart events for analytics | planned (2b) |
| `*hub_event` | free-form `{kind, ...}` | Phase 3 (race milestones, bughouse kills) | planned (3) |

Control messages Py sends to the game (handled in `tileweb.cc` `_handle_control_message`):

| `msg` | Fields | Purpose | Status |
|---|---|---|---|
| `hub_stash_list` | `items[]` {id, name, tagMajor, tagMinor} | fill the kiosk ShopMenu | planned (2a) |
| `hub_stash_withdraw` | `stashItemId`, `blob`, `tag*` | game unmarshals and inserts; replies `*hub_stash_withdraw_ok` or `_fail {reason}` | planned (2a) |
| `hub_relic_answer` | `relicId?`, `blob?`, `tag*` | generator places it or no-op | planned (2b) |
| `hub_spawn` | monster spec | bughouse (Phase 3) | planned (3) |

## 4. Rules

- Blobs are opaque to Node and Py; only the game reads them. Node stores `tagMajor`/`tagMinor` and
  refuses a withdrawal to a game reporting a different pair (the game sends its pair in `*hub_kiosk_open`).
- Hard exclusions enforced in the game at deposit (runes, the Orb, unrandarts) and re-checked by Node from
  `basetype`/`isUnrand` metadata.
- Every stash, offer and relic transition writes a ledger row in Node before the reply is sent.
- `*hub_relic_query` must never block level generation beyond its timeout; no answer means no relic.
- No game RNG is consumed by any hub call.
