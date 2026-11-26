# SPDX-License-Identifier: GPL-3.0-or-later

import shutil
import subprocess
import tempfile
from pathlib import Path

from .domain import AlignmentError, Decode

RATE = 16_000
SAMPLE_BYTES = 4
MAX_SECONDS = 900


def decode(path: Path) -> Decode:
    """Decode the first audio stream to bounded local mono float32 PCM."""
    if not path.is_file():
        raise AlignmentError("Choose a readable local audio file.")
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise AlignmentError("FFmpeg is required on PATH.")
    args = [
        ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error",
        "-protocol_whitelist", "file,pipe", "-threads", "1", "-i", str(path.resolve()),
        "-map", "0:a:0", "-t", str(MAX_SECONDS + 1), "-ac", "1", "-ar", str(RATE),
        "-f", "f32le", "pipe:1",
    ]
    with tempfile.TemporaryFile() as errors:
        try:
            run = subprocess.run(args, stdout=subprocess.PIPE, stderr=errors, timeout=150)
        except subprocess.TimeoutExpired as exc:
            raise AlignmentError("Audio decoding took more than 150 seconds.") from exc
        if run.returncode:
            errors.seek(0)
            detail = errors.read(1024).decode("utf-8", errors="replace").strip()
            raise AlignmentError(f"FFmpeg could not decode the audio. {detail}")
    frame_count = len(run.stdout) // SAMPLE_BYTES
    if frame_count > RATE * MAX_SECONDS:
        raise AlignmentError(f"Audio must be at most {MAX_SECONDS // 60} minutes long.")
    if frame_count < RATE // 10:
        raise AlignmentError("Audio must contain at least 100 ms of decoded samples.")
    return Decode(run.stdout, frame_count * 1000 // RATE)
