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


class McpTests(unittest.TestCase):
    def tearDown(self):
        server._session = None

    def test_sixteen_tools_registered(self):
        tools = asyncio.run(server.mcp.list_tools())
        self.assertEqual({tool.name for tool in tools}, {"game_launch", "game_attach", "game_close", "game_state",
                                                      "input", "wait_for", "screenshot", "console", "log",
                                                      "scene_overview", "scene_map", "actors", "teleport", "camera_state", "camera_set", "walk_to"})

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
