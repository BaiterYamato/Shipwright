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

    def test_nine_tools_registered(self):
        tools = asyncio.run(server.mcp.list_tools())
        self.assertEqual({tool.name for tool in tools}, {"game_launch", "game_attach", "game_close", "game_state",
                                                      "input", "wait_for", "screenshot", "console", "log"})

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
