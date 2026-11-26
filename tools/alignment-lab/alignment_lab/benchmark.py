# SPDX-License-Identifier: GPL-3.0-or-later

import math
import os
import statistics
import threading
import time
from pathlib import Path

from . import audio, text
from .domain import AlignmentError, Cue
from .export import lrc, serialize


def _bytes_from_status(pid: int) -> int | None:
    try:
        with open(f"/proc/{pid}/status", encoding="ascii") as status:
            for line in status:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) * 1024
    except (OSError, ValueError):
        pass
    return None


class PeakMemory:
    """Samples worker RSS plus direct decoder children, and worker GPU memory."""

    def __init__(self, device: str):
        self.ram = None
        self.vram = None
        self.pid = os.getpid()
        self.finished = threading.Event()
        self.nvml = None
        if device != "cpu":
            try:
                import pynvml
                pynvml.nvmlInit()
            except Exception:
                pass
            else:
                self.nvml = pynvml

    def _sample(self):
        own = _bytes_from_status(self.pid)
        if own is not None:
            children = set()
            for task in Path(f"/proc/{self.pid}/task").glob("*"):
                try:
                    children.update(map(int, (task / "children").read_text().split()))
                except (OSError, ValueError):
                    pass
            observed = own + sum(_bytes_from_status(pid) or 0 for pid in children)
            self.ram = max(self.ram or 0, observed)
        if self.nvml:
            nv = self.nvml
            try:
                processes = [
                    p.usedGpuMemory for index in range(nv.nvmlDeviceGetCount())
                    for p in nv.nvmlDeviceGetComputeRunningProcesses(nv.nvmlDeviceGetHandleByIndex(index))
                    if p.pid == self.pid and p.usedGpuMemory is not None
                    and 0 <= p.usedGpuMemory < (1 << 63)
                ]
                if processes:
                    self.vram = max(self.vram or 0, sum(processes))
            except nv.NVMLError:
                pass

    def _poll(self):
        while not self.finished.wait(0.01):
            self._sample()

    def __enter__(self):
        self._sample()
        self.thread = threading.Thread(target=self._poll, daemon=True)
        self.thread.start()
        return self

    def __exit__(self, *_exc):
        self.finished.set()
        self.thread.join()
        self._sample()
        if self.nvml:
            self.nvml.nvmlShutdown()


def elapsed(start: float) -> float:
    return round((time.perf_counter() - start) * 1000, 3)


LEVELS = {"lines": 1, "words": 2, "syllables": 3}
ID_FIELDS = ("line", "word", "syllable")


def reference_from(data: dict) -> dict:
    if not isinstance(data, dict) or set(data) - set(LEVELS) or not any(data.values()):
        raise AlignmentError("A reference must contain lines, words or syllables with timed entries.")
    for level, depth in LEVELS.items():
        items = data.get(level, [])
        if not isinstance(items, list):
            raise AlignmentError(f"Reference {level} must be a list.")
        ids = set()
        for item in items:
            if not isinstance(item, dict):
                raise AlignmentError("Each reference cue must be an object.")
            key = tuple(item.get(field) for field in ID_FIELDS[:depth])
            start, end = item.get("start_ms"), item.get("end_ms")
            if (any(type(index) is not int or index < 0 for index in key)
                    or type(start) is not int or start < 0
                    or (end is not None and (type(end) is not int or end < start))
                    or ("text" in item and not isinstance(item["text"], str))):
                raise AlignmentError("Reference cues require zero-based indices and nonnegative time bounds.")
            if key in ids:
                raise AlignmentError("Each reference cue must have unique indices.")
            ids.add(key)
    return data


def summarize_errors(errors: list[int]) -> dict | None:
    if not errors:
        return None
    ordered = sorted(errors)
    return {"mean_ms": round(statistics.mean(errors), 3),
            "median_ms": statistics.median(errors),
            "p95_ms": ordered[math.ceil(0.95 * len(ordered)) - 1]}


def accuracy(lines: list[Cue], reference: dict | None) -> dict | None:
    if reference is None:
        return None
    result = {}
    for level, depth in LEVELS.items():
        if not reference.get(level):
            continue
        starts, ends = [], []
        for item in reference[level]:
            children = lines
            cue = None
            for field in ID_FIELDS[:depth]:
                index = item[field]
                if index >= len(children):
                    cue = None
                    break
                cue = children[index]
                children = cue.children
            if cue is None or ("text" in item and cue.text != item["text"]):
                continue
            starts.append(abs(cue.start_ms - item["start_ms"]))
            if item.get("end_ms") is not None:
                ends.append(abs(cue.end_ms - item["end_ms"]))
        count = len(reference[level])
        result[level] = {"reference": count, "matched": len(starts),
                         "missing": count - len(starts), "start": summarize_errors(starts),
                         "end": summarize_errors(ends)}
    return result


def benchmark(entry, device, threads, path, lyrics, language, reference, warm, progress) -> dict:
    runs = []
    last = None
    backend = None

    for i in range(warm + 1):
        label = "warm" if i else "cold"
        metrics = {"phase": label, "load_ms": None, "preprocess_ms": None, "inference_ms": None,
                   "total_ms": None, "rtf": None, "duration_ms": None, "model_bytes": None,
                   "peak_ram_bytes": None, "peak_vram_bytes": None}
        error = None
        result = None
        scoring = None
        current_device = backend.device if backend is not None else None

        def report(stage, value):
            progress(f"{label.capitalize()} {i + 1}/{warm + 1} · {stage}", (i + value) / (warm + 1))

        with PeakMemory(device) as memory:
            start = time.perf_counter()
            try:
                progress(f"{label.capitalize()} {i + 1}/{warm + 1} · Starting", i / (warm + 1))
                if language not in entry.languages:
                    raise AlignmentError(f"{entry.name} does not support language {language}.")
                model_path = entry.model_file
                metrics["model_bytes"] = model_path.stat().st_size if model_path else entry.reference_bytes
                if backend is None:
                    if i:
                        raise AlignmentError("Warm run skipped because the cold load failed.")
                    stage = time.perf_counter()
                    report("Loading model", 0.02)
                    try:
                        backend = entry.load(device, threads)
                        current_device = backend.device
                    finally:
                        metrics["load_ms"] = elapsed(stage)
                else:
                    metrics["load_ms"] = 0.0
                stage = time.perf_counter()
                report("Decoding and normalizing", 0.12)
                try:
                    decoded = audio.decode(path)
                    metrics["duration_ms"] = decoded.duration_ms
                    parsed = text.normalize(lyrics, language, backend.timing == "ctc")
                finally:
                    metrics["preprocess_ms"] = elapsed(stage)
                stage = time.perf_counter()
                try:
                    lines = backend.align(decoded, parsed,
                                          lambda message, part: report(message, 0.2 + part * 0.75))
                    if not lines:
                        raise AlignmentError("The adapter returned no timing cues.")
                finally:
                    metrics["inference_ms"] = elapsed(stage)
                scoring = accuracy(lines, reference)
                result = {"timing": backend.timing, "language": language,
                          "lines": serialize(lines), "duration_ms": decoded.duration_ms,
                          "lrc": lrc(lines), "enhanced_lrc": lrc(lines, enhanced=True)}
            except (AlignmentError, OSError, UnicodeError, ValueError) as exc:
                error = str(exc)
            metrics["total_ms"] = elapsed(start)
            if metrics["duration_ms"]:
                metrics["rtf"] = round(metrics["total_ms"] / metrics["duration_ms"], 6)
        metrics["peak_ram_bytes"] = memory.ram
        metrics["peak_vram_bytes"] = memory.vram if current_device == "gpu" else None
        runs.append({"device": current_device, "metrics": metrics, "accuracy": scoring, "error": error})
        if result is not None:
            last = result

    progress("Finished", 1.0)
    return {"version": 1, "model": entry.id, "requested_device": device, "threads": threads,
            "runs": runs, "failure_rate": sum(run["error"] is not None for run in runs) / len(runs),
            "result": last}
