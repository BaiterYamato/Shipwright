import base64
import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from linkspan_playtest.overview import collect_overview, render_overview, overview_summary, _slice


def synthetic_scene():
    polygons = []
    def quad(vertices, kind="floor"):
        for a, b, c in ((0, 1, 2), (0, 2, 3)):
            polygons.append({"index": len(polygons), "kind": kind, "surface_type": 0, "room": None,
                             "vertices": [vertices[a], vertices[b], vertices[c]]})
    quad([[-800, 0, -500], [-80, 0, -500], [-80, 0, 600], [-800, 0, 600]])
    quad([[80, 180, -500], [800, 180, -500], [800, 180, 600], [80, 180, 600]])
    quad([[-80, 0, -120], [80, 180, -120], [80, 180, 120], [-80, 0, 120]])
    for x in (-800, 800):
        quad([[x, 0, -500], [x, 300, -500], [x, 300, 600], [x, 0, 600]], "wall")
    for z in (-500, 600):
        quad([[-800, 0, z], [-800, 300, z], [800, 300, z], [800, 0, z]], "wall")
    for z0, z1 in ((-500, -120), (120, 600)):
        quad([[-80, 0, z0], [-80, 300, z0], [-80, 300, z1], [-80, 0, z1]], "wall")
    transitions = [{"index": 0, "position": {"x": 0, "y": 90, "z": 0}, "front_room": 0,
        "back_room": 1, "crossing": {"normal": [1, 0, 0], "axis": "x", "semantics": "en_holl",
                                     "negative_room": 0, "positive_room": 1}},
        {"index": 1, "position": {"x": 500, "y": 180, "z": 300}, "front_room": 1, "back_room": 2,
         "crossing": {"normal": [0, 1, 0], "axis": "y", "semantics": "en_holl",
                      "negative_room": 2, "positive_room": 1}}]
    return {"scene": 85, "room": 0, "room_count": 3, "scene_epoch": 7, "frame": 20, "first_frame": 20,
        "bbox": [-800, -500, 800, 600], "y_min": -100, "y_max": 320, "stride": 1, "pages": 1,
        "partial": False, "polygons": polygons,
        "water_boxes": [{"index": 0, "x_min": -650, "z_min": 200, "x_max": -250,
                         "z_max": 480, "y": 20, "room": 0, "not_swimmable": False}],
        "transitions": transitions,
        "actors": [{"id": 21, "room": 0, "position": {"x": -360, "y": 0, "z": -250}}],
        "link": {"position": {"x": -450, "y": 0, "z": 70}, "yaw": 90, "room": 0},
        "rooms": [{"id": r} for r in range(3)], "counts": {"polygons": len(polygons), "water_boxes": 1,
        "transitions": 2, "actors": 1}, "next_offsets": {k: None for k in
        ("offset", "water_offset", "transition_offset", "actor_offset", "room_offset")},
        "has_more": False, "invalid_polygons": 0, "actors_truncated": False, "warnings": ["DynaPoly omitido."]}


def decode(png):
    compressed = bytearray()
    position = 8
    while position < len(png):
        length = struct.unpack_from(">I", png, position)[0]
        kind, data = png[position + 4:position + 8], png[position + 8:position + 8 + length]
        if kind == b"IHDR":
            width, height = struct.unpack_from(">II", data)
        if kind == b"IDAT":
            compressed.extend(data)
        position += length + 12
    raw = zlib.decompress(compressed)
    return width, height, b"".join(raw[y * (width * 3 + 1) + 1:(y + 1) * (width * 3 + 1)] for y in range(height))


class OverviewTests(unittest.TestCase):
    def test_synthetic_png_has_geometry_water_walls_link_transitions_and_text(self):
        png = render_overview(synthetic_scene(), 512)
        width, height, pixels = decode(png)
        self.assertEqual((width, height), (512, 512))
        for color in ((65, 149, 218), (24, 28, 33), (13, 123, 53), (180, 42, 139), (204, 105, 21)):
            self.assertIn(bytes(color), pixels)
        colors = {tuple(pixels[i:i + 3]) for i in range(0, len(pixels), 3)}
        self.assertGreater(len([c for c in colors if c[0] == c[1] == c[2]]), 20)  # ramp height gradient
        self.assertEqual(render_overview(synthetic_scene(), 512), png)

    def test_y_clip_really_removes_upper_floor(self):
        scene = synthetic_scene()
        scene["y_max"] = 100
        scene["water_boxes"] = []
        scene["transitions"] = []
        scene["actors"] = []
        scene["polygons"] = [p for p in scene["polygons"] if p["kind"] == "floor"]
        width, _, pixels = decode(render_overview(scene, 512))
        # Raised floor on right is outside the slice, far from the dotted grid.
        self.assertEqual(pixels[(250 * width + 400) * 3:(250 * width + 400) * 3 + 3], bytes((246, 243, 234)))
        clipped = _slice([[0, 0, 0], [100, 200, 0], [0, 0, 100]], 50, 100)
        self.assertTrue(all(50 <= v[1] <= 100 for v in clipped))
        self.assertEqual(_slice([[0, 200, 0]] * 3, 0, 100), [])

    def test_empty_degenerate_bbox_unknown_transition_and_argument_limits(self):
        scene = synthetic_scene()
        scene["polygons"] = []; scene["water_boxes"] = []
        scene["transitions"][0]["crossing"]["positive_room"] = None
        scene["transitions"][0]["crossing"]["negative_room"] = None
        self.assertTrue(render_overview(scene, 384).startswith(b"\x89PNG"))
        scene["bbox"] = [0, 0, 0, 0]
        self.assertTrue(render_overview(scene, 384).startswith(b"\x89PNG"))
        for size in (True, 10, 1281):
            with self.assertRaises(ValueError): render_overview(scene, size)
        scene["bbox"] = [1, 0, -1, 1]
        with self.assertRaises(ValueError): render_overview(scene)

    def test_collect_uses_raw_cursors_fixes_y_and_does_not_restart_finished_lists(self):
        calls = []
        first, second = synthetic_scene(), synthetic_scene()
        first["has_more"] = True; first["next_offsets"]["offset"] = 4096
        first["polygons"] = []  # a full scan can return no filtered geometry
        second["frame"] = 30
        def fetch(**args):
            calls.append(args)
            return first if len(calls) == 1 else second
        scene = collect_overview(fetch, {"limit": 128})
        self.assertEqual(calls[1]["offset"], 4096)
        self.assertEqual(calls[1]["water_offset"], 4294967295)
        self.assertEqual(calls[1]["scene_epoch"], 7)
        self.assertEqual(calls[1]["y_min"], -100)
        self.assertFalse(scene["partial"])
        self.assertEqual(scene["pages"], 2)
        self.assertEqual(scene["first_frame"], 20)
        self.assertEqual(scene["frame"], 30)

    def test_partial_summary_and_epoch_change(self):
        first = synthetic_scene(); first["has_more"] = True; first["next_offsets"]["offset"] = 128
        scene = collect_overview(lambda **_: first, {}, max_pages=1)
        summary = json.loads(overview_summary(scene))
        self.assertTrue(summary["partial"])
        self.assertEqual(summary["next_offsets"]["offset"], 128)
        self.assertEqual(summary["located_rooms"], [0, 1, 2])
        second = copy.deepcopy(first); second["scene_epoch"] += 1
        pages = iter((first, second))
        with self.assertRaisesRegex(RuntimeError, "epoch"):
            collect_overview(lambda **_: next(pages), {})
        with self.assertRaisesRegex(RuntimeError, "incompatível"):
            collect_overview(lambda **_: {}, {})
        for count in (True, 0, 65):
            with self.assertRaises(ValueError): collect_overview(lambda **_: first, {}, count)

    def test_mcp_returns_image_and_records_saved_png(self):
        import asyncio
        try:
            from linkspan_playtest import server
        except ModuleNotFoundError as error:
            if error.name == "mcp": self.skipTest("pacote mcp ausente")
            raise
        calls, records = [], []
        with tempfile.TemporaryDirectory() as folder:
            def execute(op, **args):
                calls.append((op, args)); return synthetic_scene()
            server._session = SimpleNamespace(directory=Path(folder), process=SimpleNamespace(poll=lambda: None),
                client=SimpleNamespace(execute=execute), record=lambda *args: records.append(args))
            try:
                result = asyncio.run(server.mcp.call_tool("scene_overview", {"size": 384, "include_actors": True}))
                self.assertEqual([r.type for r in result], ["text", "image"])
                self.assertEqual(result[1].mimeType, "image/png")
                saved = next(Path(folder).glob("*-scene-overview.png")).read_bytes()
                self.assertEqual(base64.b64decode(result[1].data), saved)
                self.assertEqual(calls[0][0], "scene_overview")
                self.assertTrue(calls[0][1]["include_actors"])
                self.assertEqual(records[-1][0], "scene_overview_render")
            finally:
                server._session = None


if __name__ == "__main__":
    unittest.main()
