# SPDX-License-Identifier: GPL-3.0-or-later

from dataclasses import asdict

from .domain import Cue


def stamp(ms: int) -> str:
    centiseconds = round(ms / 10)
    minutes, rest = divmod(centiseconds, 6000)
    return f"{minutes:02}:{rest // 100:02}.{rest % 100:02}"


def lrc(lines: list[Cue], enhanced: bool = False) -> str:
    result = []
    for line in lines:
        text = line.text
        if enhanced and line.children:
            # Preserve the original spelling, spaces and punctuation while
            # inserting word tags before their corresponding display text.
            parts = []
            cursor = 0
            for word in line.children:
                pos = text.find(word.text, cursor)
                if pos < 0:
                    break
                parts.append(text[cursor:pos] + f"<{stamp(word.start_ms)}>" + word.text)
                cursor = pos + len(word.text)
            else:
                text = "".join(parts) + text[cursor:] + f"<{stamp(line.end_ms)}>"
        result.append(f"[{stamp(line.start_ms)}]{text}")
    return "\n".join(result) + "\n"


def serialize(lines: list[Cue]) -> list[dict]:
    return [asdict(line) for line in lines]
