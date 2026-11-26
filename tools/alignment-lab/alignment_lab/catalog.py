# SPDX-License-Identifier: GPL-3.0-or-later

import hashlib
import importlib.util
import json
import os
import re
import shutil
from dataclasses import asdict, dataclass, replace
from functools import lru_cache
from pathlib import Path

from .domain import AlignmentError
from .heuristic import HeuristicAligner
from .storage import registry_lock, write_atomic

MMS_SOURCE = "https://huggingface.co/romara-labs/mms-300m-1130-forced-aligner-ONNX/tree/f515b917cf1f4f14d12a4768940cd8b4eb3e2119"
MMS_LICENSE = "CC-BY-NC-4.0"
HUBERT_SOURCE = "https://huggingface.co/air-air/japanese-hubert-base-phoneme-ctc-v4-ONNX/tree/a36617c5fbf7cfde02caa067716961c03cf4d87e"
ONNX_PROFILES = ("mms-fp32", "mms-q8", "hubert-ja-q4")


@lru_cache(maxsize=1)
def onnx_providers():
    try:
        import numpy  # noqa: F401 -- ONNX alignment needs NumPy.
        import onnxruntime
        return onnxruntime.get_available_providers()
    except (ImportError, OSError):
        return []


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as data:
        while block := data.read(4 * 1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


@dataclass(frozen=True)
class Entry:
    id: str
    name: str
    profile: str
    license: str
    source: str = ""
    path: str = ""
    reference_bytes: int | None = None
    reference_sha256: str = ''

    @property
    def model_file(self) -> Path | None:
        return Path(self.path) if self.path else None

    def info(self) -> dict:
        installed = bool(self.model_file and self.model_file.is_file())
        onnx = self.profile in ONNX_PROFILES
        providers = onnx_providers() if onnx and installed else []
        runtime = "CPUExecutionProvider" in providers
        has_reader = importlib.util.find_spec("pykakasi") is not None
        languages = self.languages
        if onnx and not has_reader:
            languages = [language for language in languages if language != "ja"]
        ffmpeg = shutil.which("ffmpeg") is not None
        status = ("Research only" if self.profile == "research" else
                  "FFmpeg missing" if not ffmpeg else
                  "Built in" if self.profile == "heuristic" else
                  "Model file missing" if not installed else
                  "ONNX Runtime missing" if not runtime else
                  "Japanese reading package missing" if not languages else
                  "English ready · Japanese reading package missing" if not has_reader else
                  "File registered · validated on run")
        try:
            size = self.model_file.stat().st_size if installed else self.reference_bytes
        except OSError:
            installed, size, status = False, self.reference_bytes, "Model file missing"
        return asdict(self) | {"installed": installed or self.profile == "heuristic", "status": status,
                              "runnable": bool(ffmpeg and (self.profile == "heuristic" or (installed and runtime and languages))),
                              "model_bytes": size, "can_assign": onnx, "languages": languages,
                              "devices": (["auto", "cpu", "gpu"]
                                          if self.profile == "mms-fp32" and "CUDAExecutionProvider" in providers
                                          else ["auto", "cpu"] if self.profile != "research" else [])}

    @property
    def languages(self):
        return ["ja"] if self.profile == "hubert-ja-q4" else ["ja", "en"] if self.profile != "research" else []

    def load(self, device: str, threads: int):
        if self.profile == "heuristic":
            if device == "gpu":
                raise AlignmentError("Heuristic Sync runs on CPU.")
            return HeuristicAligner()
        if self.profile not in ONNX_PROFILES:
            raise AlignmentError(f"{self.name} has no installed adapter.")
        if not self.model_file or not self.model_file.is_file():
            raise AlignmentError(f"Choose a local ONNX file for {self.name}.")
        from .onnx_ctc import OnnxCtcAligner
        return OnnxCtcAligner(self.model_file, device, threads, self.profile)


BUILT_INS = {
    entry.id: entry for entry in (
        Entry("heuristic", "Heuristic Sync", "heuristic", "GPL-3.0-or-later (implementation)",
              reference_bytes=0),
        Entry("mms-fp32", "MMS-300M ONNX (FP32)", "mms-fp32", MMS_LICENSE, MMS_SOURCE,
              reference_bytes=1_262_408_964,
              reference_sha256="26b2c0ba8f11ed045424ae1a70bc0b76f080952fb62fab5a37b3b39d82f08391"),
        Entry("mms-q8", "MMS-300M ONNX (INT8)", "mms-q8", MMS_LICENSE, MMS_SOURCE,
              reference_bytes=356_289_622,
              reference_sha256="bef3271b8f4496fd173da1d2392c4bc7d496e0245cf1e8700481f812795b4abe"),
        Entry("hubert-ja-q4", "Japanese HuBERT v4 ONNX (INT4)", "hubert-ja-q4", "Apache-2.0",
              HUBERT_SOURCE, reference_bytes=63_010_793,
              reference_sha256="0151f5bd899a86aab0d34c06dba74689e60a95e6dce0a6642b6c005b74aef630"),
        Entry("pyshiro", "pyshiro / SHIRO Japanese singing HSMM", "research",
              "GPL-3.0 (code); separate checkpoint", "https://github.com/wavtechyukky/pyshiro"),
        Entry("qwen3", "Qwen3-ForcedAligner-0.6B", "research", "Apache-2.0",
              "https://huggingface.co/Qwen/Qwen3-ForcedAligner-0.6B", reference_bytes=1_835_544_544),
        Entry("sofa", "SOFA singing aligner", "research", "MIT (code); verify checkpoint terms",
              "https://github.com/qiuqiao/SOFA"),
        Entry("whisper", "WhisperX / whisper.cpp", "research", "BSD-2-Clause / MIT (see research)",
              "https://github.com/m-bain/whisperX"),
    )
}
MODEL_ID = re.compile("[a-z][a-z0-9-]{0,47}")


def registry_path() -> Path:
    override = os.environ.get("ALIGNMENT_LAB_REGISTRY")
    if override:
        return Path(override)
    config = Path(os.environ.get("XDG_CONFIG_HOME") or (Path.home() / ".config"))
    return config / "swiftify-alignment-lab" / "models.json"


class Catalog:
    def __init__(self):
        self.file = registry_path()
        self._read()

    def _read(self):
        self.entries = BUILT_INS.copy()
        self.registered: dict[str, Entry] = {}
        if self.file.exists():
            try:
                data = json.loads(self.file.read_text(encoding="utf-8"))
                if data["version"] != 1:
                    raise ValueError("unsupported registry version")
                self.registered = {e["id"]: Entry(**e) for e in data["models"]}
                self.entries.update(self.registered)
            except (KeyError, ValueError, TypeError) as exc:
                raise AlignmentError(f"Invalid model registry: {self.file}") from exc

    def select(self, model_id: str) -> Entry:
        try:
            return self.entries[model_id]
        except KeyError as exc:
            raise AlignmentError(f"Unknown model: {model_id}") from exc

    def register(self, model_id: str, profile: str, path: Path, name: str, license: str, source: str):
        path = path.expanduser().resolve()
        if not MODEL_ID.fullmatch(model_id):
            raise AlignmentError("Model ID must use lower-case ASCII letters, digits or hyphens.")
        if not path.is_file() or path.suffix.lower() != ".onnx":
            raise AlignmentError("Choose a readable local .onnx file.")
        if profile not in ONNX_PROFILES:
            raise AlignmentError("Choose a supported ONNX profile.")
        builtin = BUILT_INS.get(model_id)
        if builtin and builtin.profile != profile:
            raise AlignmentError("This identifier is reserved for the corresponding built-in profile.")
        if builtin and builtin.reference_sha256 and (
                path.stat().st_size != builtin.reference_bytes or sha256(path) != builtin.reference_sha256):
            raise AlignmentError("The file does not match this preset. Register a compatible variant with a custom ID.")
        if not builtin and (not name.strip() or not license.strip() or not source.strip()):
            raise AlignmentError("Enter the name, license and source of the custom export.")
        entry = (replace(builtin, path=str(path)) if builtin else
                 Entry(model_id, name.strip(), profile, license.strip(), source.strip(), str(path)))
        with registry_lock(self.file):
            self._read()
            self.registered[model_id] = entry
            self._save()

    def remove(self, model_id: str):
        with registry_lock(self.file):
            self._read()
            if model_id not in self.registered:
                raise AlignmentError("There is no local registration for this model.")
            del self.registered[model_id]
            self._save()

    def _save(self):
        write_atomic(self.file, json.dumps({"version": 1, "models": [
            asdict(entry) for entry in self.registered.values()]}, ensure_ascii=False, indent=2))
        self.entries = BUILT_INS | self.registered
