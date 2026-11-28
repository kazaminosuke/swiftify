# SPDX-License-Identifier: GPL-3.0-or-later

import hashlib
import json
import os
from pathlib import Path

from .storage import write_atomic

CACHE_VERSION = 1


def cache_file(audio: Path, model: Path | None, model_id: str, profile: str,
               lyrics: str, language: str, device: str, threads: int) -> Path:
    def identity(path):
        if path is None:
            return None
        stat = path.stat()
        return [str(path.resolve()), stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns,
                stat.st_dev, stat.st_ino]

    key = [CACHE_VERSION, identity(audio), identity(model), model_id, profile,
           lyrics, language, device, threads]
    digest = hashlib.sha256(json.dumps(key, ensure_ascii=False).encode("utf-8")).hexdigest()
    base = Path(os.environ.get("XDG_CACHE_HOME") or (Path.home() / ".cache"))
    return base / "swiftify-alignment-lab" / f"{digest}.json"


def load(path: Path) -> dict | None:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) and data.get("result") else None
    except (OSError, ValueError):
        return None


def save(path: Path, result: dict):
    write_atomic(path, json.dumps(result, ensure_ascii=False))
