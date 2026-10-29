import hmac
import logging
import secrets

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


def handle_game_message(process, msgobj):
    """Server-only ``*hub_*`` messages from a crawl process.

    Slice 2a-1: log and drop. Slice 2a-2 forwards them to the Node hub and
    answers withdrawals. ``process`` is the CrawlProcessHandlerBase that
    received the message; ``process.username`` names the player.
    """
    log = logging.getLogger("hub")
    kind = msgobj.get("msg")
    if kind == "hub_stash_deposit":
        log.info("hub deposit from %s: %s x%s (%s) at %s, blob %d chars",
                 process.username, msgobj.get("name"), msgobj.get("quantity"),
                 "randart" if msgobj.get("isRandart") else "plain",
                 msgobj.get("place"), len(msgobj.get("blob", "")))
    else:
        log.info("hub message %s from %s (not handled yet)", kind,
                 process.username)
