#!/usr/bin/env python3
"""The plank timer does not deep-sleep, so it does not use the demo key-wake guard."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PlankKeyWakeContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.main = (ROOT / "main/main.c").read_text(encoding="utf-8")

    def test_timer_does_not_enter_deep_sleep_or_pretend_to_guard_it(self) -> None:
        # Button wake from deep sleep can replay the still-held key. The demo
        # menu drops that key in on_key. This application never sleeps, so that
        # guard is not wired; adding sleep means bringing the guard back too.
        self.assertNotIn("esp_deep_sleep", self.main)
        self.assertNotIn("esp_sleep_enable", self.main)
        self.assertNotIn("key_guard_", self.main)


if __name__ == "__main__":
    unittest.main()
