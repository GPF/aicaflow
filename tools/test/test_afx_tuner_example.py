#!/usr/bin/env python3
"""Transport-free check for the documented tuner upload example."""

import client
import example
import struct
import sys
import tempfile
from pathlib import Path


calls = []
client.request = lambda _host, opcode, payload=b"": (
    calls.append((opcode, payload)) or
    ((0, struct.pack("<I", client.RESET)) if opcode == client.PING else
     (0, bytes(28)) if opcode == client.STATUS else (0, b"")))
client.upload_staged = lambda _host, _payload, commit, progress=False: (
    calls.append((commit, progress)) or (0, b""))
client.validate_bank = lambda _payload: None
client.validate_control = lambda _payload: None
client.validate_seek_index = lambda _payload: None

with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    bank, control, index = directory / "music.afb", directory / "title.afx", directory / "title.afc"
    for path in (bank, control, index): path.write_bytes(b"test")
    old_argv = sys.argv
    sys.argv = ["tuner-example", str(bank), str(control), "--index", str(index), "--reset",
                "--region", "12000", "8000"]
    try:
        assert example.main() == 0
    finally:
        sys.argv = old_argv

assert calls == [(client.PING, b""), (client.RESET, b""),
                 (client.BANK_COMMIT, True), (client.CONTROL_COMMIT_PLAY, True),
                 (client.SEEK_INDEX_COMMIT_PLAY, True),
                 (client.PLAY_REGION, struct.pack("<II", 12000, 8000)),
                 (client.STATUS, b"")]
print("tuner example checks passed")
