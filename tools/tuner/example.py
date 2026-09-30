#!/usr/bin/env python3
"""Upload one bank-bound AFX flow to the persistent Dreamcast tuner.

Example:
    python3 tools/tuner/example.py music.afb title.afx --index title.afc --reset
"""

import argparse
import json
import struct
from pathlib import Path

import client


def checked(result: int, action: str) -> None:
    if result:
        raise RuntimeError(f"{action} failed: {result}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bank", type=Path, help="AFB sample bank")
    parser.add_argument("control", type=Path, help="bank-bound AFX control flow")
    parser.add_argument("--index", type=Path, help="optional AFC seek index")
    parser.add_argument("--host", default="10.0.0.184")
    parser.add_argument("--reset", action="store_true", help="empty AICAflow before uploading")
    parser.add_argument("--region", nargs=2, type=int, metavar=("START_MS", "DURATION_MS"),
                        help="audition one region after upload")
    args = parser.parse_args()

    result, data = client.request(args.host, client.PING)
    checked(result, "ping")
    if len(data) != 4:
        raise RuntimeError("tuner did not return its opcode capability")
    max_opcode, = struct.unpack("<I", data)
    if args.reset:
        if max_opcode < client.RESET:
            raise RuntimeError("tuner is too old for reset; rebuild and reload tools/tuner/server")
        result, _ = client.request(args.host, client.RESET)
        checked(result, "reset")

    bank = args.bank.read_bytes()
    control = args.control.read_bytes()
    client.validate_bank(bank)
    client.validate_control(control)
    result, _ = client.upload_staged(args.host, bank, client.BANK_COMMIT, progress=True)
    checked(result, "bank upload")
    result, _ = client.upload_staged(args.host, control, client.CONTROL_COMMIT_PLAY, progress=True)
    checked(result, "control upload")
    if args.index:
        index = args.index.read_bytes()
        client.validate_seek_index(index)
        result, _ = client.upload_staged(args.host, index, client.SEEK_INDEX_COMMIT_PLAY, progress=True)
        checked(result, "seek-index upload")
    if args.region:
        start, duration = args.region
        if start < 0 or not 1 <= duration <= 30000:
            raise ValueError("region needs START_MS >= 0 and DURATION_MS 1..30000")
        result, _ = client.request(args.host, client.PLAY_REGION, struct.pack("<II", start, duration))
        checked(result, "region playback")

    result, data = client.request(args.host, client.STATUS)
    checked(result, "status")
    if len(data) != 28:
        raise RuntimeError("tuner returned an invalid status payload")
    state, afx_result, position, detail, timer, heartbeat, asset = struct.unpack("<7I", data)
    print(json.dumps({"state": state, "result": afx_result, "position": position,
                      "detail": detail, "timer_ticks": timer, "heartbeat": heartbeat,
                      "asset": asset}, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        raise SystemExit(f"afx-tuner-example: {error}")
