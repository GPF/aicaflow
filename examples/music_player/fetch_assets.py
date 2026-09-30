#!/usr/bin/env python3
"""Fetch the pinned public-domain scores and optional GeneralUser input bank."""

from __future__ import annotations

import hashlib
from io import BytesIO
from pathlib import Path
import sys
from urllib.request import urlopen
from zipfile import ZipFile

SOURCES = (
    ("chopin-op28-no20.mid", "https://www.mutopiaproject.org/ftp/ChopinFF/O28/Chop-28-20/Chop-28-20.mid", "9cc6c417d4bb261644f6bd3d00033025e675a25694fd172be2f98e93efc1fcfe"),
    ("grieg-mountain-king.mid", "https://www.mutopiaproject.org/ftp/GriegE/O46/Dans_l_antre_du_roi_de_la_montagne/Dans_l_antre_du_roi_de_la_montagne.mid", "0c256a809b5af1faef81b6aad6106088a71d76b12435d5f0775d4ceee60c917e"),
    ("GeneralUser.sf2", "https://raw.githubusercontent.com/ad-si/GeneralUser/master/GeneralUser.sf2", "f45b6b4a68b6bf3d792fcbb6d7de24dc701a0f89c5900a21ef3aaece993b839a"),
)
BACH_URL = "https://www.mutopiaproject.org/ftp/BachJS/BWV1007/bwv1007/bwv1007-mids.zip"
BACH_NAME = "bach-bwv1007-prelude.mid"
BACH_MEMBER = "bwv1007-1.mid"
BACH_SHA256 = "59449437fd1455537d3fd9d8963eac4a9f2d413f0d446b39539edbc7090fe619"


def checked(path: Path, expected: str) -> bool:
    return path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == expected


def download(url: str) -> bytes:
    with urlopen(url) as response:
        return response.read()


def put(path: Path, data: bytes, expected: str) -> None:
    digest = hashlib.sha256(data).hexdigest()
    if digest != expected:
        raise RuntimeError(f"{path.name}: expected {expected}, got {digest}")
    path.write_bytes(data)
    print(f"fetched {path.name}")


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        print(f"usage: {Path(sys.argv[0]).name} DIRECTORY", file=sys.stderr)
        return 2
    directory = Path(argv[0])
    directory.mkdir(parents=True, exist_ok=True)
    for name, url, digest in SOURCES:
        path = directory / name
        if checked(path, digest):
            print(f"verified {path.name}")
        else:
            put(path, download(url), digest)
    bach = directory / BACH_NAME
    if checked(bach, BACH_SHA256):
        print(f"verified {bach.name}")
    else:
        with ZipFile(BytesIO(download(BACH_URL))) as archive:
            put(bach, archive.read(BACH_MEMBER), BACH_SHA256)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
