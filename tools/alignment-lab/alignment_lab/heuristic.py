# SPDX-License-Identifier: GPL-3.0-or-later

from .domain import Cue, Decode, Lyric, Progress


def distribute(start: int, end: int, weights: list[int]) -> list[tuple[int, int]]:
    span = end - start
    total = sum(weights)
    cursor = 0
    ranges = []
    for weight in weights:
        ranges.append((start + span * cursor // total, start + span * (cursor + weight) // total))
        cursor += weight
    return ranges


class HeuristicAligner:
    device = "cpu"
    timing = "estimated"

    def align(self, audio: Decode, lyrics: list[Lyric], progress: Progress) -> list[Cue]:
        """Distribute time by text length, including any instrumental sections."""
        bounds = distribute(0, audio.duration_ms, [max(1, len(line.text)) for line in lyrics])
        result = []
        for line, (start, end) in zip(lyrics, bounds):
            words = []
            word_bounds = distribute(start, end, [max(1, len(word.text)) for word in line.words])
            for word, (left, right) in zip(line.words, word_bounds):
                syllables = [Cue(part.text, a, b) for part, (a, b) in zip(
                    word.syllables, distribute(left, right, [1] * len(word.syllables)))]
                words.append(Cue(word.text, left, right, syllables))
            result.append(Cue(line.text, start, end, words))
        progress("Estimating timings", 1.0)
        return result
