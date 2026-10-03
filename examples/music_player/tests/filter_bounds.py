#!/usr/bin/env python3
"""Reject AFX setup images with invalid AICA filter levels."""

from pathlib import Path
import struct
import sys


for path_text in sys.argv[1:]:
    path = Path(path_text)
    data = path.read_bytes()
    image_offset, setup_count = struct.unpack_from("<II", data, 16)[0], struct.unpack_from("<I", data, 36)[0]
    for setup in range(setup_count):
        for field in range(11, 16):
            value = struct.unpack_from("<H", data, image_offset + setup * 36 + field * 2)[0]
            assert value <= 0x1FF7, f"{path}: setup {setup}, filter field {field} is {value:#x}"
