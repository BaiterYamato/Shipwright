from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from linkspan_playtest.conditions import parse, wait_for


class ConditionsTests(unittest.TestCase):
    def test_comparisons(self):
        state = {"scene": 85, "game_state": "play", "textbox": False, "frame": 1200,
                 "position": {"x": -1.25}, "room": None}
        for condition in ('scene == 85 and game_state == "play"', 'textbox == false', 'frame >= 1200',
                          'scene != 86 and scene <= 85 and scene > 84', 'position.x < -1.2', 'room == null'):
            with self.subTest(condition=condition):
                self.assertTrue(parse(condition)(state))
        self.assertFalse(parse('textbox == 0')(state))
        self.assertFalse(parse('room > 0')(state))
        self.assertFalse(parse('health == null')(state))
        self.assertFalse(parse('scene == 86')(state))
        self.assertTrue(parse('game_state == "and"')({"game_state": "and"}))

    def test_invalid(self):
        for value in ('', 'scene', 'scene = 2', 'scene == 2 or frame > 1', 'scene == 2 and',
                      '__import__("os")', 'scene == 2; print(1)', 'unknown == 2', 'frame > NaN',
                      'frame > 1e999', 'scene == 85 junk', 'frame > True', 'game_state == \'play\''):
            with self.subTest(value=value), self.assertRaises(ValueError):
                parse(value)

    def test_wait_and_timeout(self):
        class Fake:
            frame = 0
            def state(self):
                self.frame += 1
                return {"frame": self.frame}
        fake = Fake()
        self.assertEqual(wait_for(fake, "frame >= 3", 1, 0.001)["frame"], 3)
        with self.assertRaises(TimeoutError):
            wait_for(fake, "frame < 0", 0.01, 0.001)


if __name__ == "__main__":
    unittest.main()
