import json
import os
import sqlite3

import tornado.gen
import tornado.testing
import tornado.web

from webtiles import auth, config, userdb
from webtiles.hub import bridge


class HubBridgeTest(tornado.testing.AsyncHTTPTestCase):
    def setUp(self):
        self.saved_config = config.server_config
        config.server_config = dict(
            settings_db="./unittest_hub_settings.db3",
            password_db="./unittest_hub_passwd.db3",
            dgl_mode=True,
            hub_enabled=True,
            hub_bridge_secret="s3cret",
        )
        try:
            userdb.init_db_connections(quiet=True)
        except (sqlite3.Warning, sqlite3.Error):
            self.tearDown()
            raise
        super(HubBridgeTest, self).setUp()

    def tearDown(self):
        super(HubBridgeTest, self).tearDown()
        for db in (userdb.user_db, userdb.settings_db):
            if db:
                db.close()
        for f in ("./unittest_hub_settings.db3", "./unittest_hub_passwd.db3"):
            if os.path.exists(f):
                os.unlink(f)
        config.server_config = self.saved_config

    def get_app(self):
        return tornado.web.Application([
            (r"/hub/token", bridge.HubTokenHandler),
            (r"/hub/games", bridge.HubGamesHandler),
        ])

    def post_token(self, body, secret="s3cret"):
        return self.fetch("/hub/token", method="POST", body=json.dumps(body),
                          headers={"X-Hub-Secret": secret}, raise_error=False)

    def test_requires_secret(self):
        r = self.post_token({"username": "alice"}, secret="wrong")
        self.assertEqual(r.code, 403)

    def test_disabled_is_404(self):
        config.server_config["hub_enabled"] = False
        r = self.post_token({"username": "alice"})
        self.assertEqual(r.code, 404)

    def test_mints_token_and_creates_user(self):
        self.assertIsNone(userdb.get_user_info("alice"))
        r = self.post_token({"username": "alice"})
        self.assertEqual(r.code, 200, r.body)
        data = json.loads(r.body)
        self.assertEqual(data["username"], "alice")
        self.assertIsNotNone(userdb.get_user_info("alice"))
        username, ok = auth.check_login_cookie(data["token"])
        self.assertTrue(ok)
        self.assertEqual(username, "alice")
        # a second call reuses the account and issues a fresh token
        r2 = self.post_token({"username": "alice"})
        self.assertEqual(r2.code, 200)
        self.assertNotEqual(json.loads(r2.body)["token"], data["token"])

    def test_rejects_bad_names(self):
        self.assertEqual(self.post_token({"username": "no spaces here"}).code, 400)
        self.assertEqual(self.post_token({}).code, 400)

    def test_games_list_is_json(self):
        r = self.fetch("/hub/games", headers={"X-Hub-Secret": "s3cret"})
        self.assertEqual(r.code, 200)
        self.assertEqual(json.loads(r.body), [])


class FakeNodeHandler(tornado.web.RequestHandler):
    """Stands in for the Node hub's POST /bridge/game."""
    calls = []

    def post(self):
        FakeNodeHandler.calls.append((self.request.headers.get("X-Hub-Secret"),
                                      json.loads(self.request.body)))
        ev = json.loads(self.request.body)["event"]
        if ev["msg"] == "hub_kiosk_open":
            reply = {"msg": "hub_stash_list", "items": [{"id": 7, "name": "x"}]}
        elif ev["msg"] == "hub_item_event":
            reply = None
        else:
            reply = {"msg": "hub_stash_deposit_ok", "stashItemId": 1}
        self.write(json.dumps({"reply": reply}))


class FakeProcess(object):
    username = "alice"

    def __init__(self):
        self.sent = []
        self.conn = self

    def send_message(self, data):
        self.sent.append(json.loads(data))


class HandleGameMessageTest(HubBridgeTest):
    def get_app(self):
        # The test app doubles as the fake Node hub.
        return tornado.web.Application([
            (r"/hub/token", bridge.HubTokenHandler),
            (r"/hub/games", bridge.HubGamesHandler),
            (r"/hub/control", bridge.HubControlHandler),
            (r"/bridge/game", FakeNodeHandler),
        ])

    def setUp(self):
        super(HandleGameMessageTest, self).setUp()
        FakeNodeHandler.calls = []
        config.server_config["hub_node_url"] = self.get_url("")

    @tornado.testing.gen_test
    async def test_relays_and_answers(self):
        p = FakeProcess()
        bridge.handle_game_message(p, {"msg": "hub_kiosk_open", "xl": 1})
        bridge.handle_game_message(p, {"msg": "hub_item_event"})
        # Both relays run concurrently, so wait for them and look the kiosk
        # call up by message instead of by arrival order.
        for _ in range(50):
            if len(FakeNodeHandler.calls) == 2 and p.sent:
                break
            await tornado.gen.sleep(0.05)
        self.assertEqual(len(FakeNodeHandler.calls), 2)
        kiosk = [c for c in FakeNodeHandler.calls
                 if c[1]["event"]["msg"] == "hub_kiosk_open"]
        self.assertEqual(len(kiosk), 1)
        secret, body = kiosk[0]
        self.assertEqual(secret, "s3cret")
        self.assertEqual(body["username"], "alice")
        self.assertEqual(p.sent, [{"msg": "hub_stash_list",
                                   "items": [{"id": 7, "name": "x"}]}])

    @tornado.testing.gen_test
    async def test_unreachable_hub_fails_requests_that_expect_replies(self):
        config.server_config["hub_node_url"] = "http://127.0.0.1:1"
        config.server_config["hub_node_timeout"] = 0.5
        p = FakeProcess()
        bridge.handle_game_message(p, {"msg": "hub_stash_withdraw_req",
                                       "stashItemId": 5})
        bridge.handle_game_message(p, {"msg": "hub_item_event"})
        await tornado.gen.sleep(1.0)
        self.assertEqual(p.sent, [{"msg": "hub_stash_withdraw_fail",
                                   "reason": "the hub is unreachable",
                                   "stashItemId": 5}])

    def test_control_push_reaches_the_running_game(self):
        from webtiles import process_handler
        p = FakeProcess()
        process_handler.processes["/tmp/fake.sock"] = p
        try:
            r = self.fetch("/hub/control", method="POST",
                           body=json.dumps({"username": "Alice",
                                            "control": {"msg": "hub_spawn",
                                                        "monster": "rat"}}),
                           headers={"X-Hub-Secret": "s3cret"})
            self.assertEqual(r.code, 200)
            self.assertEqual(json.loads(r.body), {"delivered": True})
            self.assertEqual(p.sent, [{"msg": "hub_spawn", "monster": "rat"}])
            r = self.fetch("/hub/control", method="POST",
                           body=json.dumps({"username": "nobody",
                                            "control": {"msg": "hub_spawn"}}),
                           headers={"X-Hub-Secret": "s3cret"})
            self.assertEqual(json.loads(r.body), {"delivered": False})
        finally:
            del process_handler.processes["/tmp/fake.sock"]
