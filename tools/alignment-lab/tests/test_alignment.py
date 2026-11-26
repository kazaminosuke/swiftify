# SPDX-License-Identifier: GPL-3.0-or-later

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import wave
from pathlib import Path
from unittest.mock import patch

from alignment_lab import benchmark, ctc, export, text
from alignment_lab.catalog import Catalog
from alignment_lab.domain import AlignmentError, Cue


class AlignmentTests(unittest.TestCase):
    def test_reference_reports_missing_and_never_estimates_ground_truth(self):
        cues = [Cue("line", 60, 100)]
        self.assertIsNone(benchmark.accuracy(cues, None))
        reference = benchmark.reference_from({"lines": [
            {"line": 0, "start_ms": 50, "end_ms": 105},
            {"line": 1, "start_ms": 150},
        ]})
        scored = benchmark.accuracy(cues, reference)["lines"]
        self.assertEqual((scored["matched"], scored["missing"]), (1, 1))
        self.assertEqual(scored["start"], {"mean_ms": 10, "median_ms": 10, "p95_ms": 10})
        self.assertEqual(scored["end"]["p95_ms"], 5)
        with self.assertRaises(AlignmentError):
            benchmark.reference_from({"words": [{"line": 0, "word": -1, "start_ms": 20}]})

    def test_enhanced_export_preserves_punctuation_and_repeated_tokens(self):
        line = Cue("Go, go!", 320, 1230, [Cue("Go", 320, 620), Cue("go", 740, 1230)])
        self.assertEqual(export.lrc([line], enhanced=True),
                         "[00:00.32]<00:00.32>Go, <00:00.74>go!<00:01.23>\n")
        self.assertEqual(export.lrc([line]), "[00:00.32]Go, go!\n")
        self.assertEqual(export.serialize([line])[0]["children"][1]["start_ms"], 740)

    def test_model_registration_preserves_other_workers_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            model = Path(directory) / "manual.onnx"
            model.touch()
            registry = Path(directory) / "registry.json"
            with patch.dict(os.environ, {"ALIGNMENT_LAB_REGISTRY": str(registry)}):
                first = Catalog()
                second = Catalog()
                first.register("first", "mms-q8", model, "A", "CC-BY-NC-4.0", "example")
                second.register("second", "mms-q8", model, "B", "CC-BY-NC-4.0", "example")
                fresh = Catalog()
                self.assertEqual(set(fresh.registered), {"first", "second"})
                fresh.remove("first")
                self.assertTrue(model.is_file())
                self.assertEqual(set(Catalog().registered), {"second"})
                self.assertFalse(fresh.select("qwen3").info()["runnable"])

    def test_ctc_requires_a_blank_between_repeated_tokens(self):
        try:
            import numpy as np
        except ImportError:
            self.skipTest("NumPy is optional")
        logits = np.asarray([[0, 5], [5, 0], [0, 5]], dtype=np.float32)
        bounds = ctc.path_bounds(logits, [1, 1], lambda *args: None)
        self.assertEqual(bounds, [(0, 1), (2, 3)])
        with self.assertRaises(AlignmentError):
            ctc.path_bounds(logits[:2], [1, 1], lambda *args: None)

    def test_japanese_morae_group_small_kana(self):
        self.assertEqual(text.morae("きょうも"), ["きょ", "う", "も"])

    @unittest.skipUnless(shutil.which("ffmpeg"), "FFmpeg is optional for unit tests")
    def test_cli_reuses_model_in_warm_run_and_exports(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            audio = root / "one.wav"
            lyrics = root / "lyrics.txt"
            out = root / "out.lrc"
            with wave.open(str(audio), "wb") as writer:
                writer.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
                writer.writeframes(bytes(32_000))
            lyrics.write_text("hello world\nsecond line\n", encoding="utf-8")
            args = [sys.executable, "-B", "-m", "alignment_lab", "benchmark",
                    "--model", "heuristic", "--audio", str(audio), "--lyrics", str(lyrics),
                    "--language", "en", "--warm", "1", "--output", str(out), "--format", "lrc"]
            run = subprocess.run(args, capture_output=True, check=True)
            report = json.loads(run.stdout)
            first, second = report["runs"]
            self.assertEqual(first["metrics"]["phase"], "cold")
            self.assertEqual(second["metrics"]["phase"], "warm")
            self.assertEqual(second["metrics"]["load_ms"], 0)
            self.assertIsNone(second["accuracy"])
            self.assertEqual(report["result"]["timing"], "estimated")
            self.assertIn("[00:00.00]hello world", out.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
