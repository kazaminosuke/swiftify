# SPDX-License-Identifier: GPL-3.0-or-later

import re
import unicodedata
from functools import lru_cache

from .domain import AlignmentError, Lyric, Part, Word

MAX_TEXT_BYTES = 64 * 1024
MAX_LINES = 600
LATIN_WORD = re.compile(r"[^\W_]+(?:['’][^\W_]+)*", re.UNICODE)
JAPANESE_TEXT = re.compile(r"[ぁ-ゖァ-ヺー々\u3400-\u9fff]+|[^\W_]+(?:['’][^\W_]+)*")
KANA = re.compile(r"^[ぁ-ゖァ-ヺー]+$")
SMALL_KANA = frozenset("ぁぃぅぇぉゃゅょゎァィゥェォャュョヮ")
ASCII_TARGET = re.compile("[a-z']+")


def latin(text: str) -> str:
    text = unicodedata.normalize("NFKD", text.lower().replace("’", "'"))
    return "".join(c for c in text if c.isascii() and (c.isalpha() or c == "'"))


def morae(kana: str) -> list[str]:
    parts: list[str] = []
    for c in kana:
        if c in SMALL_KANA and parts and parts[-1] not in {"っ", "ッ", "ー"}:
            parts[-1] += c
        else:
            parts.append(c)
    return parts


@lru_cache(maxsize=1)
def japanese_reader():
    try:
        from pykakasi import kakasi
    except ImportError:
        return None
    return kakasi()


def mora_targets(kana: str, reader) -> tuple[Part, ...]:
    pieces = morae(kana)
    targets = [latin("".join(item["hepburn"] for item in reader.convert(piece)))
               for piece in pieces]
    for index, piece in enumerate(pieces):
        if piece in {"っ", "ッ"}:
            # Use the onset of the following mora when it is available.
            following = targets[index + 1] if index + 1 < len(targets) else ""
            targets[index] = (following[:1].replace("c", "t")
                              if following[:1] not in {"", "a", "e", "i", "o", "u"}
                              else "")
        elif piece == "ー":
            previous = targets[index - 1] if index else ""
            targets[index] = next((c for c in reversed(previous) if c in "aeiou"), "")
    return tuple(Part(piece, target) for piece, target in zip(pieces, targets))


def japanese_words(text: str, use_ctc: bool) -> list[Word]:
    reader = japanese_reader()
    if not reader:
        if use_ctc:
            raise AlignmentError("Japanese MMS alignment requires the optional pykakasi package.")
        return [
            Word(token, latin(token), tuple(Part(p, "") for p in morae(token))
                 if KANA.fullmatch(token) else ())
            for token in JAPANESE_TEXT.findall(text)
        ]
    words = []
    for item in reader.convert(text):
        for token in JAPANESE_TEXT.findall(item["orig"]):
            if all(c.isascii() for c in token):
                words.append(Word(token, latin(token)))
            elif token == item["orig"] and KANA.fullmatch(item["hira"]):
                parts = mora_targets(item["hira"], reader)
                target = "".join(part.target for part in parts)
                # Drop all mora boundaries for a word if any mora lacks a target.
                words.append(Word(token, target, parts if all(p.target for p in parts) else ()))
            else:
                words.append(Word(token, latin(token)))
    return words


def normalize(text: str, language: str, use_ctc: bool) -> list[Lyric]:
    if len(text.encode("utf-8")) > MAX_TEXT_BYTES:
        raise AlignmentError("Lyrics exceed 64 KiB.")
    lines = [line.strip() for line in unicodedata.normalize("NFKC", text.lstrip("\ufeff")).splitlines()
             if line.strip()]
    if not lines or len(lines) > MAX_LINES:
        raise AlignmentError(f"Enter between 1 and {MAX_LINES} nonempty lyric lines.")
    if language not in {"ja", "en"}:
        raise AlignmentError("The current adapters support Japanese and English.")
    lyrics = []
    for index, line in enumerate(lines, start=1):
        words = (japanese_words(line, use_ctc) if language == "ja"
                 else [Word(token, latin(token)) for token in LATIN_WORD.findall(line)])
        if use_ctc and (not words or any(not ASCII_TARGET.fullmatch(w.target) for w in words)
                        or any(c.isdigit() for c in line)):
            raise AlignmentError(f"Line {index} contains text without a CTC reading. Spell out numbers and check the language.")
        lyrics.append(Lyric(line, words))
    return lyrics
