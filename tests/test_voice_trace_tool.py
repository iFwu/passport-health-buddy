#!/usr/bin/env python3
import importlib.util
import json
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("voice_trace_tool", Path(__file__).resolve().parents[1] / "tools/voice-trace.py")
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)


class TraceCaptureTests(unittest.TestCase):
    def test_only_structured_trace_is_retained(self):
        row = {"run": "run-a", "turn": 1, "stage": "asr.final", "text": "卧推60公斤8个\n下一行"}
        encoded = b"I (20) voice_trace: " + json.dumps(row, ensure_ascii=False).encode() + b"\r\n"
        self.assertEqual(trace.trace_row(encoded), row)
        self.assertIsNone(trace.trace_row(b"I (20) wifi: private network name\n"))
        self.assertIsNone(trace.trace_row(b"I (20) voice_trace: {broken\n"))
        self.assertIsNone(trace.trace_row(b"I (20) voice_trace: []\n"))

    def test_delayed_asr_joins_only_matching_run_and_input(self):
        rows = [
            {"run": "a", "turn": 0, "stage": "asr.final", "input": "i1", "text": "first"},
            {"run": "a", "turn": 1, "stage": "input_audio_buffer.committed", "input": "i1"},
            {"run": "a", "turn": 2, "stage": "input_audio_buffer.committed", "input": "i2"},
            {"run": "a", "turn": 0, "stage": "asr.final", "input": "i1", "text": "delayed"},
            {"run": "b", "turn": 0, "stage": "asr.final", "input": "i1", "text": "other boot"},
            {"run": "a", "turn": 0, "stage": "asr.final", "input": "unknown", "text": "unknown"},
            {"run": "a", "turn": 0, "stage": "session.config"},
        ]
        groups = trace.group_turns(rows)
        self.assertEqual([r.get("text") for r in groups[("a", 1)]], ["first", None, "delayed"])
        self.assertEqual(len(groups[("a", 2)]), 1)
        self.assertEqual(groups[("b", 0)][0]["text"], "other boot")
        self.assertEqual(groups[("a", 0)][0]["text"], "unknown")
        self.assertEqual(rows[0]["turn"], 0)


if __name__ == "__main__":
    unittest.main()
