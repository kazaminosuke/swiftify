# SPDX-License-Identifier: GPL-3.0-or-later

from .domain import AlignmentError, Cue

FRAME_MS = 20
MAX_ALIGNMENT_CELLS = 64_000_000
# Token IDs of the pinned MMS-300M-1130 ONNX vocabulary. Other MMS exports
# may have a different order or an extra separator column.
VOCAB = {c: i for i, c in enumerate("aienout srmkldghybpwcvjzf'qx".replace(" ", ""), 4)}
BLANK = 0


def path_bounds(logits, target: list[int], progress):
    """Monotonic CTC Viterbi with explicit blanks and repeated-label handling."""
    import numpy as np

    frames = len(logits)
    required = len(target) + sum(a == b for a, b in zip(target, target[1:]))
    if not target or required > frames:
        raise AlignmentError("There are too few audio frames for the supplied lyrics.")
    states = 2 * len(target) + 1
    if frames * states > MAX_ALIGNMENT_CELLS:
        raise AlignmentError("The CTC trellis exceeds the memory limit. Split the song or lyrics.")
    if not np.isfinite(logits).all():
        raise AlignmentError("The model returned non-finite CTC scores.")

    # Log-softmax would subtract the same per-frame constant from all paths.
    labels = np.zeros(states, dtype=np.intp)
    labels[1::2] = target
    skip = (labels[2:] != BLANK) & (labels[2:] != labels[:-2])
    back = np.empty((frames, states), dtype=np.uint8)
    previous = np.full(states, -np.inf, dtype=np.float32)
    previous[0] = 0
    changes = np.empty(states, dtype=np.uint8)
    for t, frame in enumerate(logits):
        best = previous.copy()
        changes.fill(0)
        step = previous[:-1] > best[1:]
        np.copyto(best[1:], previous[:-1], where=step)
        changes[1:][step] = 1
        jump = skip & (previous[:-2] > best[2:])
        np.copyto(best[2:], previous[:-2], where=jump)
        changes[2:][jump] = 2
        back[t] = changes
        previous = best + frame[labels]
        if t % max(1, frames // 20) == 0:
            progress("Tracing CTC alignment", 0.82 + t / frames * 0.17)

    state = states - 1 if previous[-1] >= previous[-2] else states - 2
    if not np.isfinite(previous[state]):
        raise AlignmentError("No complete CTC path was found.")
    starts = [frames] * len(target)
    ends = [0] * len(target)
    for t in range(frames - 1, -1, -1):
        if state % 2:
            token = state // 2
            starts[token] = t
            ends[token] = max(ends[token], t + 1)
        state -= int(back[t, state])
    return list(zip(starts, ends))


def cues_from_path(lyrics, bounds, duration_ms: int, token_count=len) -> list[Cue]:
    def cue(text, first, last, children=None):
        start = min(duration_ms, bounds[first][0] * FRAME_MS)
        end = min(duration_ms, bounds[last - 1][1] * FRAME_MS)
        return Cue(text, start, end, children or [])

    offset = 0
    lines = []
    for line in lyrics:
        first = offset
        words = []
        for word in line.words:
            left = offset
            syllables = []
            for part in word.syllables:
                end = offset + token_count(part.target)
                syllables.append(cue(part.text, offset, end))
                offset = end
            offset = left + token_count(word.target)
            words.append(cue(word.text, left, offset, syllables))
        lines.append(cue(line.text, first, offset, words))
    return lines
