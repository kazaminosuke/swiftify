# SPDX-License-Identifier: GPL-3.0-or-later

from dataclasses import dataclass, field
from typing import Callable, Protocol


class AlignmentError(Exception):
    pass


Progress = Callable[[str, float], None]


@dataclass(frozen=True)
class Part:
    text: str
    target: str


@dataclass(frozen=True)
class Word:
    text: str
    target: str
    syllables: tuple[Part, ...] = ()


@dataclass(frozen=True)
class Lyric:
    text: str
    words: list[Word]


@dataclass(frozen=True)
class Cue:
    text: str
    start_ms: int
    end_ms: int
    children: list["Cue"] = field(default_factory=list)


@dataclass(frozen=True)
class Decode:
    samples: bytes
    duration_ms: int


class Aligner(Protocol):
    device: str
    timing: str

    def align(self, audio: Decode, lyrics: list[Lyric], progress: Progress) -> list[Cue]:
        ...
