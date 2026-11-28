# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare private fixtures from the official JSUT-song recording and labels."""

import argparse
import json
import re
import subprocess
from pathlib import Path
from zipfile import ZipFile

# Reading and phrase boundaries for song 070. The official full-context
# annotations supply all times. Three contiguous /a/ notes in the last
# phrase form the same sung mora.
PHRASES = [
    [("うさぎ", ["u", "s a", "g i"]), ("うさぎ", ["u", "s a", "g i"]),
     ("なに", ["n a", "n i"]), ("みて", ["m i", "t e"]), ("はねる", ["h a", "n e", "r u"])],
    [("じゅうごや", ["j u", "u", "g o", "y a"]), ("おつきさま", ["o", "ts u", "k i", "s a", "m a"]),
     ("みて", ["m i", "t e"]), ("はねる", ["h a a a", "n e", "r u"])],
]


def prepare(audio_zip: Path, label_zip: Path, dest: Path):
    dest.mkdir(parents=True, exist_ok=True)
    audio = dest / "jsut-070.wav"
    mixed = dest / "jsut-070-mix.wav"
    with ZipFile(audio_zip) as bundle:
        audio.write_bytes(bundle.read("jsut-song_ver1/child_song/wav/070.wav"))
    with ZipFile(label_zip) as bundle:
        labels = bundle.read("todai_child/070.lab").decode().splitlines()
    phones = []
    for annotation in labels:
        start, end, context = annotation.split(maxsplit=2)
        # Only parse the central phone, before later full-context fields.
        token = re.search(r"-([^+]+)\+", context.split("/", 1)[0]).group(1)
        if token not in {"pau", "sil", "br"}:
            phones.append((token, round(int(start) / 10_000), round(int(end) / 10_000)))

    cursor = 0
    truth = {"lines": [], "words": [], "syllables": []}
    for line_index, words in enumerate(PHRASES):
        phrase_begin = phones[cursor][1]
        for word_index, (word, syllables) in enumerate(words):
            word_begin = phones[cursor][1]
            for part_index, reading in enumerate(syllables):
                expected = reading.split()
                selected = phones[cursor:cursor + len(expected)]
                if [phoneme for phoneme, _, _ in selected] != expected:
                    raise ValueError("Unexpected phoneme sequence in the official 070.lab.")
                truth["syllables"].append({"line": line_index, "word": word_index, "syllable": part_index,
                                           "start_ms": selected[0][1], "end_ms": selected[-1][2]})
                cursor += len(expected)
            truth["words"].append({"line": line_index, "word": word_index, "text": word,
                                   "start_ms": word_begin, "end_ms": phones[cursor - 1][2]})
        truth["lines"].append({"line": line_index, "start_ms": phrase_begin,
                               "end_ms": phones[cursor - 1][2]})
    if cursor != len(phones):
        raise ValueError("Unconsumed phoneme labels in 070.lab.")
    (dest / "jsut-070.txt").write_text("\n".join(" ".join(w for w, _ in line) for line in PHRASES),
                                     encoding="utf-8")
    (dest / "jsut-070.json").write_text(json.dumps(truth, ensure_ascii=False, indent=2), encoding="utf-8")
    # Add a quiet, phase-stable backing bed without moving the reference clock.
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(audio), "-f", "lavfi", "-i",
                    "aevalsrc=0.025*(sin(2*PI*220*t)+sin(2*PI*329.63*t)+sin(2*PI*440*t))*(0.6+0.4*cos(2*PI*2*t)):s=48000",
                    "-filter_complex", "[0:a][1:a]amix=inputs=2:duration=first:normalize=0",
                    str(mixed)], check=True)
    manifest = [
        {"id": item.stem, "audio": item.name, "lyrics": "jsut-070.txt", "language": "ja",
         "kind": "singing", "reference": "jsut-070.json"} for item in (audio, mixed)
    ]
    (dest / "singing.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("audio_zip", type=Path)
    parser.add_argument("label_zip", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    prepare(args.audio_zip, args.label_zip, args.destination)
