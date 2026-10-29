import json
import os
import sqlite3

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


class HandleGameMessageTest(HubBridgeTest):
    def test_logs_and_drops_deposit(self):
        class P(object):
            username = "alice"
        with self.assertLogs("hub", level="INFO") as cm:
            bridge.handle_game_message(P(), {
                "msg": "hub_stash_deposit", "name": "a potion of curing",
                "quantity": 2, "isRandart": False, "place": "D:1",
                "blob": "AAAA", "tagMajor": 34, "tagMinor": 1})
            bridge.handle_game_message(P(), {"msg": "hub_item_event"})
        self.assertTrue(any("hub deposit from alice" in m for m in cm.output))
        self.assertTrue(any("hub_item_event" in m for m in cm.output))
