import asyncio
import base64
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
try:
    from linkspan_playtest import server
except ModuleNotFoundError as error:
    # O runner do CI não instala o pacote mcp; os outros módulos de teste não dependem dele.
    if error.name != "mcp":
        raise
    raise unittest.SkipTest("pacote mcp ausente") from None
from linkspan_playtest.capture import png_rgb
from test_vfs import observed


class McpTests(unittest.TestCase):
    def tearDown(self):
        server._session = None

    def test_eighteen_tools_registered(self):
        tools = asyncio.run(server.mcp.list_tools())
        self.assertEqual({tool.name for tool in tools}, {"game_launch", "game_attach", "game_close", "game_state",
                                                      "input", "wait_for", "screenshot", "console", "log",
                                                      "scene_overview", "scene_map", "actors", "teleport", "camera_state", "camera_set", "walk_to",
                                                      "vfs_layers", "game_vfs_snapshot"})

    def test_new_tools_forward_arguments_and_record_results(self):
        requests, records = [], []
        def execute(op, **arguments):
            requests.append((op, arguments))
            return {"status": "refused", "reason": "cutscene"}
        server._session = SimpleNamespace(process=SimpleNamespace(poll=lambda: None),
                                          client=SimpleNamespace(execute=execute),
                                          record=lambda *args: records.append(args))
        self.assertEqual(server.scene_map()["status"], "refused")
        server.actors(radius=100, category=4)
        server.teleport(1, 2, 3, yaw=90)
        server.camera_state()
        server.camera_set(yaw=90, pitch=-30, distance=100)
        server.camera_set(mode="normal")
        server.camera_set(eye=[1, 2, 3], at=[0, 0, 0])
        server.walk_to(100, 200, tolerance=10, timeout=30)
        self.assertEqual(requests[1], ("actors", {"radius": 100, "category": 4, "offset": 0, "limit": 128}))
        self.assertEqual(requests[2][1]["yaw"], 90)
        self.assertEqual(requests[5], ("camera_set", {"mode": "normal"}))
        self.assertEqual(requests[-1], ("walk_to", {"x": 100, "z": 200, "tolerance": 10, "timeout": 30}))
        self.assertEqual(len(records), 2 * len(requests))

    def test_vfs_tools_and_close_finalize_actual_snapshot(self):
        with tempfile.TemporaryDirectory() as folder:
            requests, records = [], []
            directory = Path(folder)
            session = SimpleNamespace(directory=directory, metadata={"sha256": "b" * 64},
                                      process=SimpleNamespace(poll=lambda: None),
                                      record=lambda *a: records.append(a))
            def execute(op, **args):
                requests.append((op, args))
                return observed(args["paths"])
            session.client = SimpleNamespace(execute=execute)
            def close():
                session.metadata["closed"] = True
                session.process.poll = lambda: 0
                (directory / "game-final.log").write_bytes(b"ready\nunload\n")
                return {"exit_code": 0}
            session.close = close
            server._session = session
            self.assertTrue(server.vfs_layers(["test/layers.json"])["complete"])
            self.assertEqual(server.game_vfs_snapshot("wire", "after-ready", ["test/layers.json"])["status"],
                             "pending_post_log")
            result = server.game_close()
            self.assertEqual(len(result["vfs_snapshots"]), 1)
            self.assertTrue(Path(result["vfs_snapshots"][0]).exists())
            self.assertIsNone(server._session)
            self.assertEqual(requests[0][0], "vfs_layers")
            self.assertEqual(len(records), 4)

    def test_screenshot_mcp_returns_real_image(self):
        png = png_rgb(1, 1, b"\xff\x80\x01")
        with tempfile.TemporaryDirectory() as folder:
            server._session = SimpleNamespace(directory=Path(folder), process=SimpleNamespace(pid=1, poll=lambda: None),
                                              record=lambda *args: None)
            def capture(pid, path):
                return {"path": str(path), "focus_used": False, "width": 1, "height": 1, "png": png}
            with patch.object(server, "capture_window", capture):
                result = asyncio.run(server.mcp.call_tool("screenshot", {"label": "proof"}))
            self.assertEqual([item.type for item in result], ["text", "image"])
            self.assertIn("focus_used: false", result[0].text)
            self.assertEqual(base64.b64decode(result[1].data), png)
            with self.assertRaises(ValueError):
                server.screenshot("../outside")

    def test_input_waits_past_end_frame(self):
        class Client:
            frame = 10
            def input(self, steps):
                return {"queued": len(steps), "start_frame": 11, "end_frame": 12}
            def state(self):
                self.frame += 1
                return {"frame": self.frame}
        client = Client()
        server._session = SimpleNamespace(process=SimpleNamespace(poll=lambda: None), record=lambda *args: None, client=client)
        result = server.input([{"frames": 2}])
        self.assertEqual(result["state"]["frame"], 13)


if __name__ == "__main__":
    unittest.main()
