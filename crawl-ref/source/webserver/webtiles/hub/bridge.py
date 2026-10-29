import hmac
import logging
import secrets

import tornado.httpclient
import tornado.ioloop
import tornado.web
from tornado.escape import json_decode, json_encode

from webtiles import auth, config, status, userdb

LOOPBACK = ("127.0.0.1", "::1")


class HubHandlerMixin(object):
    """Shared-secret + loopback gate for every /hub/ endpoint."""

    def prepare(self):
        secret = config.get('hub_bridge_secret')
        if not config.get('hub_enabled') or not secret:
            raise tornado.web.HTTPError(404)
        if self.request.remote_ip not in LOOPBACK:
            raise tornado.web.HTTPError(403, "hub bridge is loopback only")
        given = self.request.headers.get("X-Hub-Secret", "")
        if not hmac.compare_digest(given, secret):
            raise tornado.web.HTTPError(403, "bad hub secret")


class HubTokenHandler(HubHandlerMixin, tornado.web.RequestHandler):
    """POST {"username": ...} -> {"token": cookie}

    The cookie is accepted by the websocket ``token_login`` message. The
    webtiles account is created on first use with an unguessable password:
    the hub owns authentication, webtiles only needs a matching row.
    """

    def post(self):
        try:
            body = json_decode(self.request.body or b"{}")
            username = str(body["username"]).strip()
        except (ValueError, KeyError, TypeError):
            raise tornado.web.HTTPError(400, "expected JSON with username")
        if not config.check_name(username):
            raise tornado.web.HTTPError(400, "invalid username")
        info = userdb.get_user_info(username)
        if info is None:
            err = userdb.register_user(username, secrets.token_urlsafe(32), "")
            if err:
                logging.getLogger("hub").warning(
                    "Could not create webtiles user %s: %s", username, err)
                raise tornado.web.HTTPError(500, err)
            logging.getLogger("hub").info("Created webtiles user %s for hub", username)
            info = userdb.get_user_info(username)
        if userdb.dgl_is_banned(info.flags):
            raise tornado.web.HTTPError(403, "user is banned")
        token = auth.log_in_as_user(self, info.username)
        self.set_header("Content-Type", "application/json")
        self.write(json_encode({"username": info.username, "token": token}))

    def set_cookie(self, *args, **kwargs):
        # auth.log_in_as_user sets a browser cookie for plain HTTP handlers;
        # the hub relays the token over its own channel instead.
        pass


class HubGamesHandler(HubHandlerMixin, status.LobbyHandler):
    """GET -> the public lobby JSON (same shape as /status/lobby/)."""


class HubControlHandler(HubHandlerMixin, tornado.web.RequestHandler):
    """POST {"username": ..., "control": {...}} -> {"delivered": bool}

    Pushes an unsolicited control message (e.g. ``hub_spawn``) to the
    running game of ``username``. Used for bughouse and other live effects.
    """

    def post(self):
        try:
            body = json_decode(self.request.body or b"{}")
            username = str(body["username"]).strip().lower()
            control = body["control"]
            assert isinstance(control, dict) and "msg" in control
        except (ValueError, KeyError, TypeError, AssertionError):
            raise tornado.web.HTTPError(400, "expected JSON with username and control")
        from webtiles.process_handler import processes
        delivered = False
        for process in list(processes.values()):
            if (process.username or "").lower() == username and process.conn:
                process.conn.send_message(json_encode(control))
                delivered = True
                break
        self.set_header("Content-Type", "application/json")
        self.write(json_encode({"delivered": delivered}))


REPLY_EXPECTED = {
    "hub_kiosk_open": "hub_stash_list",
    "hub_stash_deposit": "hub_stash_deposit_fail",
    "hub_stash_withdraw_req": "hub_stash_withdraw_fail",
}


def _node_url():
    return config.get('hub_node_url') or "http://127.0.0.1:6500"


def handle_game_message(process, msgobj):
    """Server-only ``*hub_*`` messages from a crawl process.

    Relayed to the Node hub as ``POST /bridge/game`` with the shared secret;
    the hub's reply (if any) is sent back to the game as a control message.
    When the hub is unreachable, requests that expect an answer get a
    ``*_fail`` message so the game never waits on a dead link.
    ``process`` is the CrawlProcessHandlerBase that received the message.
    """
    tornado.ioloop.IOLoop.current().spawn_callback(_relay, process, msgobj)


async def _relay(process, msgobj):
    log = logging.getLogger("hub")
    kind = msgobj.get("msg")
    username = process.username
    body = json_encode({"username": username, "event": msgobj})
    reply = None
    try:
        client = tornado.httpclient.AsyncHTTPClient()
        resp = await client.fetch(
            _node_url() + "/bridge/game", method="POST", body=body,
            headers={"X-Hub-Secret": config.get('hub_bridge_secret') or "",
                     "Content-Type": "application/json"},
            request_timeout=config.get('hub_node_timeout') or 3.0)
        reply = json_decode(resp.body).get("reply")
        log.info("hub relay %s from %s -> %s", kind, username,
                 reply.get("msg") if reply else "no reply")
    except Exception as e:  # noqa: BLE001 - any failure means "hub down"
        log.warning("hub relay %s from %s failed: %s", kind, username, e)
        fail = REPLY_EXPECTED.get(kind)
        if fail:
            reply = {"msg": fail, "reason": "the hub is unreachable"}
            if kind == "hub_stash_withdraw_req":
                reply["stashItemId"] = msgobj.get("stashItemId")
    if reply and process.conn:
        process.conn.send_message(json_encode(reply))
