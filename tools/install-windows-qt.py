#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install the pinned Qt MSVC kit used by the Windows prebuilt workflow."""

import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import urllib.request


def install(destination: Path) -> None:
    manifest = Path(__file__).with_name("qt-6.12.1-windows-msvc.sha256")
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        raise FileExistsError(destination)

    with tempfile.TemporaryDirectory(dir=destination.parent, prefix="swiftify-qt-") as staging:
        root = Path(staging)
        kit = root / "kit"
        kit.mkdir()
        for row in manifest.read_text(encoding="ascii").splitlines():
            expected, url = row.split()
            name = url.rsplit("/", 1)[1]
            tarball = root / name
            checksum = hashlib.sha256()
            print(f"Fetching {name}", file=sys.stderr)
            with urllib.request.urlopen(url, timeout=60) as response, tarball.open("xb") as archive:
                while block := response.read(1024 * 1024):
                    checksum.update(block)
                    archive.write(block)
            if checksum.hexdigest() != expected:
                raise ValueError(f"Qt checksum mismatch for {name}")
            subprocess.run(["cmake", "-E", "tar", "xf", str(tarball)], cwd=kit, check=True)
            tarball.unlink()

        if not (kit / "bin" / "windeployqt.exe").is_file():
            raise ValueError("Missing Qt deployment tool in the extracted kit")
        (kit / "bin" / "qt.conf").write_text("[Paths]\nPrefix=..\n", encoding="ascii")
        shutil.copyfile(manifest, kit / manifest.name)
        kit.rename(destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    install(parser.parse_args().destination.resolve())
