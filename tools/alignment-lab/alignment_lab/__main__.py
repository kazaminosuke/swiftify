# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

from . import cache
from .benchmark import benchmark, reference_from
from .catalog import Catalog, ONNX_PROFILES
from .domain import AlignmentError
from .storage import write_atomic
from .text import MAX_TEXT_BYTES


def read_lyrics(path: str) -> str:
    if path == "-":
        raw = sys.stdin.buffer.read(MAX_TEXT_BYTES + 1)
    else:
        with Path(path).expanduser().open("rb") as source:
            raw = source.read(MAX_TEXT_BYTES + 1)
    if len(raw) > MAX_TEXT_BYTES:
        raise AlignmentError("Lyrics exceed 64 KiB.")
    return raw.decode("utf-8-sig")


def read_json(path: str | Path):
    with Path(path).expanduser().open(encoding="utf-8") as source:
        return json.load(source)


def emit(value: dict):
    print(json.dumps(value, ensure_ascii=False), flush=True)


def run(args, catalog):
    entry = catalog.select(args.model)
    lyrics = read_lyrics(args.lyrics)
    path = Path(args.audio).expanduser()
    truth = reference_from(read_json(args.reference)) if args.reference else None
    if args.output:
        destination = Path(args.output).expanduser().resolve()
        inputs = (path, entry.model_file, Path(args.lyrics) if args.lyrics != "-" else None,
                  Path(args.reference) if args.reference else None, catalog.file)
        if any(item is not None and destination == item.expanduser().resolve() for item in inputs):
            raise AlignmentError("Choose an export path different from the inputs and registry.")
    memo = None
    if args.cache:
        if args.command != "align" or truth is not None or args.device == "auto":
            raise AlignmentError("Cache is available for align with an explicit device and no reference.")
        memo = cache.cache_file(path, entry.model_file, entry.id, entry.profile, lyrics,
                                args.language, args.device, args.threads)
    report = cache.load(memo) if memo else None
    if report is not None:
        report["runs"] = []
        report["failure_rate"] = None
        report["cached"] = True
    else:
        def progress(stage, value):
            if args.events:
                emit({"event": "progress", "stage": stage, "value": value})

        report = benchmark(entry, args.device, args.threads, path, lyrics, args.language, truth,
                           args.warm if args.command == "benchmark" else 0, progress)
        if memo and report["result"]:
            try:
                if memo == cache.cache_file(path, entry.model_file, entry.id, entry.profile, lyrics,
                                            args.language, args.device, args.threads):
                    cache.save(memo, report)
            except OSError:
                # Successful alignment remains usable if the cache becomes unavailable.
                pass

    if args.output and report["result"] is not None:
        result = report["result"]
        contents = (json.dumps(result, ensure_ascii=False, indent=2) + "\n"
                    if args.format == "json" else result[args.format])
        write_atomic(Path(args.output), contents)
    emit({"event": "result", "report": report} if args.events else report)
    return 0 if report["failure_rate"] in (None, 0.0) else 1


def suite(args):
    manifest = Path(args.manifest).expanduser().resolve()
    data = read_json(manifest)
    if not isinstance(data, list) or not data:
        raise AlignmentError("Expected a nonempty benchmark manifest array.")
    runs = []
    signature = {"audio", "lyrics", "language", "kind"}
    for item in data:
        if (not isinstance(item, dict) or not signature.issubset(item)
                or item["kind"] not in ("speech", "singing", "synthetic")):
            raise AlignmentError("Each case requires audio, lyrics, language and kind.")
        for model in args.model:
            command = [sys.executable, "-m", "alignment_lab", "benchmark", "--model", model,
                       "--device", args.device, "--threads", str(args.threads), "--warm", str(args.warm),
                       "--audio", str(manifest.parent / item["audio"]),
                       "--lyrics", str(manifest.parent / item["lyrics"]), "--language", item["language"]]
            if item.get("reference"):
                command += ["--reference", str(manifest.parent / item["reference"])]
            worker = subprocess.run(command, stdout=subprocess.PIPE, text=True, encoding="utf-8",
                                    env=os.environ | {"PYTHONIOENCODING": "utf-8"})
            try:
                report = json.loads(worker.stdout)
                report.pop("result", None)
            except ValueError:
                report = {"failure_rate": 1.0, "error": f"Worker exited with code {worker.returncode}."}
            runs.append({"case": item.get("id", item["audio"]), "kind": item["kind"],
                         "language": item["language"], "model": model, "report": report})
    emit({"version": 1, "cases": runs})
    return int(any(run["report"].get("failure_rate", 1.0) for run in runs))


def bounded(low, high):
    def parse(value):
        number = int(value)
        if not low <= number <= high:
            raise argparse.ArgumentTypeError(f"Choose a value between {low} and {high}.")
        return number
    return parse


def parser():
    root = argparse.ArgumentParser(description="Offline alignment and benchmarking")
    commands = root.add_subparsers(dest="command", required=True)
    commands.add_parser("models", help="List adapters, model files and licenses")
    add = commands.add_parser("register", help="Register a local ONNX file")
    add.add_argument("--id", required=True)
    add.add_argument("--profile", required=True, choices=ONNX_PROFILES)
    add.add_argument("--file", required=True)
    add.add_argument("--name", default="")
    add.add_argument("--license", default="")
    add.add_argument("--source", default="")
    commands.add_parser("remove", help="Remove a registration").add_argument("--id", required=True)
    for command in ("align", "benchmark", "suite"):
        work = commands.add_parser(command)
        work.add_argument("--model", action="append" if command == "suite" else "store", required=True)
        work.add_argument("--device", choices=("cpu", "gpu", "auto"), default="cpu")
        work.add_argument("--threads", type=bounded(1, 32), default=2)
        if command != "align":
            work.add_argument("--warm", type=bounded(0, 30), default=2)
        if command == "suite":
            work.add_argument("manifest")
        else:
            work.add_argument("--audio", required=True)
            work.add_argument("--lyrics", required=True, help="UTF-8 plain text, or - for stdin")
            work.add_argument("--language", choices=("ja", "en"), required=True)
            work.add_argument("--reference")
            work.add_argument("--events", action="store_true", help="Emit JSON lines for progress and results")
            work.add_argument("--cache", action="store_true")
            work.add_argument("--output")
            work.add_argument("--format", choices=("json", "lrc", "enhanced_lrc"), default="json")
    return root


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    args = parser().parse_args()
    try:
        if args.command == "suite":
            return suite(args)
        catalog = Catalog()
        if args.command == "models":
            emit({"models": [entry.info() | {"registered": entry.id in catalog.registered}
                             for entry in catalog.entries.values()]})
        elif args.command == "register":
            catalog.register(args.id, args.profile, Path(args.file), args.name, args.license, args.source)
        elif args.command == "remove":
            catalog.remove(args.id)
        else:
            return run(args, catalog)
    except (AlignmentError, OSError, UnicodeError, ValueError) as exc:
        if getattr(args, "events", False):
            emit({"event": "error", "message": str(exc)})
        else:
            print(str(exc), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    sys.exit(main())
