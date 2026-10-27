from pathlib import Path
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from linkspan_playtest.capture import png_rgb


class PngTests(unittest.TestCase):
    def test_chunks_crc_and_pixels(self):
        pixels = bytes((255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255))
        png = png_rgb(2, 2, pixels)
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        pos, kinds, compressed = 8, [], bytearray()
        while pos < len(png):
            size = struct.unpack_from(">I", png, pos)[0]
            kind, data = png[pos + 4:pos + 8], png[pos + 8:pos + 8 + size]
            crc = struct.unpack_from(">I", png, pos + 8 + size)[0]
            self.assertEqual(crc, zlib.crc32(kind + data) & 0xffffffff)
            if kind == b"IHDR":
                self.assertEqual(struct.unpack(">IIBBBBB", data), (2, 2, 8, 2, 0, 0, 0))
            if kind == b"IDAT":
                compressed.extend(data)
            kinds.append(kind)
            pos += 12 + size
        self.assertEqual(pos, len(png))
        self.assertEqual(kinds, [b"IHDR", b"IDAT", b"IEND"])
        self.assertEqual(zlib.decompress(compressed), b"\0" + pixels[:6] + b"\0" + pixels[6:])

    def test_bad_dimensions(self):
        for width, height, pixels in ((0, 1, b""), (1, -1, b""), (1, 1, b"x")):
            with self.assertRaises(ValueError):
                png_rgb(width, height, pixels)


if __name__ == "__main__":
    unittest.main()
