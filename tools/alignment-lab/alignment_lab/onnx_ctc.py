# SPDX-License-Identifier: GPL-3.0-or-later

from pathlib import Path

from . import audio, ctc
from .domain import AlignmentError, Decode, Lyric, Part, Progress, Word

STRIDE = 320
RECEPTIVE_FIELD = 400
WINDOW = 12 * audio.RATE
CONTEXT = audio.RATE


# Ordered token IDs from prj-beatrice's pinned HuBERT v4 vocabulary.
HUBERT_VOCAB = {token: index for index, token in enumerate(
    "PAD UNK SOS EOS a i u e o I U k g s z t d n h b p m y r w f j v N cl sh ch ts ky gy hy by py my ny ry fy ty dy kw gw pau sil".split()
)}


def phonemes(part: Part) -> str:
    if part.text in {"っ", "ッ"}:
        return "cl"
    if part.text in {"ん", "ン"}:
        return "N"
    onset, vowel = part.target[:-1], part.target[-1:]
    if vowel not in "aiueo" or not vowel or (onset and onset not in HUBERT_VOCAB):
        raise AlignmentError(f"No HuBERT phoneme mapping for mora {part.text}. Enter kana readings.")
    return f"{onset} {vowel}".strip()


def phoneme_lyrics(lyrics):
    result = []
    for line in lyrics:
        words = []
        for word in line.words:
            if not word.syllables:
                raise AlignmentError("Japanese HuBERT requires kana readings for each word.")
            parts = tuple(Part(part.text, phonemes(part)) for part in word.syllables)
            words.append(Word(word.text, " ".join(part.target for part in parts), parts))
        result.append(Lyric(line.text, words))
    return result


class OnnxCtcAligner:
    timing = "ctc"

    def __init__(self, path: Path, device: str, threads: int, profile: str):
        try:
            import numpy as np
            import onnxruntime as ort
        except ImportError as exc:
            raise AlignmentError("Install NumPy and the optional ONNX Runtime.") from exc
        self.hubert = profile == "hubert-ja-q4"
        cpu_only = profile != "mms-fp32"
        self.np = np
        options = ort.SessionOptions()
        options.intra_op_num_threads = threads
        options.inter_op_num_threads = 1
        options.log_severity_level = 3
        cuda = not cpu_only and "CUDAExecutionProvider" in ort.get_available_providers()
        if device == "gpu" and not cuda:
            reason = ("This quantized export is supported on CPU."
                      if cpu_only else "The ONNX Runtime CUDA provider is unavailable.")
            raise AlignmentError(reason)
        try:
            if device != "cpu" and cuda:
                self.session = ort.InferenceSession(str(path), options, providers=[
                    "CUDAExecutionProvider", "CPUExecutionProvider"])
                if "CUDAExecutionProvider" not in self.session.get_providers():
                    if device == "gpu":
                        raise AlignmentError("ONNX Runtime could not activate CUDA.")
            else:
                self.session = ort.InferenceSession(str(path), options, providers=["CPUExecutionProvider"])
        except Exception as exc:
            raise AlignmentError(f"ONNX Runtime could not load this model: {exc}") from exc

        self.session.disable_fallback()
        self.device = ("gpu" if "CUDAExecutionProvider" in self.session.get_providers() else "cpu")
        specs = {item.name: item for item in self.session.get_inputs()}
        self.has_mask = "attention_mask" in specs
        expected = {"input_values", "attention_mask"} if self.has_mask else {"input_values"}
        if set(specs) != expected or specs["input_values"].type != "tensor(float)" or (
                self.has_mask and specs["attention_mask"].type != "tensor(int64)"):
            raise AlignmentError("Expected the ONNX input_values / attention_mask signature.")
        output = next((item for item in self.session.get_outputs() if item.name == "logits"), None)
        if output is None or output.type != "tensor(float)" or len(output.shape) != 3:
            raise AlignmentError("Expected a float logits output [1, frames, vocabulary].")
        vocab_size = 48 if self.hubert else 31
        if isinstance(output.shape[-1], int) and output.shape[-1] != vocab_size:
            raise AlignmentError("The ONNX vocabulary size differs from the registered profile.")

    def _emissions(self, decoded: Decode, progress: Progress):
        np = self.np
        pcm = np.frombuffer(decoded.samples, dtype="<f4")
        emissions = []
        for start in range(0, len(pcm), WINDOW):
            block_start = max(0, start - CONTEXT)
            center_end = min(len(pcm), start + WINDOW)
            end = min(len(pcm), center_end + CONTEXT)
            clip = pcm[block_start:end]
            normalized = clip if self.hubert else (clip - clip.mean()) / np.sqrt(clip.var() + np.float32(1e-7))
            feed = {"input_values": normalized[None, :]}
            if self.has_mask:
                feed["attention_mask"] = np.ones((1, len(clip)), dtype=np.int64)
            try:
                result = self.session.run(["logits"], feed)[0]
            except Exception as exc:
                raise AlignmentError(f"ONNX inference failed: {exc}") from exc
            expected_frames = (len(clip) - RECEPTIVE_FIELD) // STRIDE + 1
            if result.shape != (1, expected_frames, 48 if self.hubert else 31):
                raise AlignmentError("ONNX frame or vocabulary layout differs from the selected profile.")
            left = (start - block_start) // STRIDE
            right = expected_frames if center_end == len(pcm) else (center_end - block_start) // STRIDE
            emissions.append(result[0, left:right])
            progress("Inferring CTC emissions", min(0.8, 0.8 * center_end / len(pcm)))
        return np.concatenate(emissions)

    def align(self, audio: Decode, lyrics: list[Lyric], progress: Progress):
        vocab = HUBERT_VOCAB if self.hubert else ctc.VOCAB
        if self.hubert:
            lyrics = phoneme_lyrics(lyrics)

        def tokens(target):
            return target.split() if self.hubert else list(target)

        ids = [vocab[token] for line in lyrics for word in line.words for token in tokens(word.target)]
        bounds = ctc.path_bounds(self._emissions(audio, progress), ids, progress)
        progress("Building timing cues", 1.0)
        return ctc.cues_from_path(lyrics, bounds, audio.duration_ms, lambda target: len(tokens(target)))
