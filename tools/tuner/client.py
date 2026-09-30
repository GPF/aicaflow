#!/usr/bin/env python3
"""Client for the persistent Dreamcast Aicaflow tuner on TCP port 31337."""

import argparse
import json
import socket
import struct
import sys
from pathlib import Path


MAGIC, VERSION, PORT = 0x31544641, 1, 31337
AFX_MAGIC, AFX_ABI, AFX_HEADER_BYTES = 0x32584641, 7, 80
AFB_MAGIC, AFB_VERSION, AFB_HEADER_BYTES = 0x00424641, 1, 32
AFC_MAGIC, AFC_VERSION, AFC_HEADER_BYTES = 0x00434641, 1, 32
PING, UPLOAD, PLAY, STOP, PATCH, INSTANCE_GAIN, STATUS, EXIT, UPLOAD_PLAY, UPLOAD_BEGIN, UPLOAD_CHUNK, UPLOAD_COMMIT, DSP_ENABLE, DSP_DISABLE, ASSET_READ, PLAY_REGION, MESSAGE, DSP_PROGRAM, REGISTER_READ, BANK_COMMIT, CONTROL_COMMIT_PLAY, DSP_ROOM, LANE_MUTE, DSP_RETURNS, DSP_PROGRAM_RING, SEEK_INDEX_COMMIT_PLAY = range(1, 27)
CHUNK_BYTES = 32768
TCP_WRITE_BYTES = 1024


def message_payload(text: str) -> bytes:
    payload = text.encode("ascii")
    if not 1 <= len(payload) <= 79 or any(c < 32 or c > 126 for c in payload):
        raise ValueError("message needs 1..79 printable ASCII characters")
    return payload


def receive(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("Dreamcast closed the tuner connection")
        data.extend(chunk)
    return bytes(data)


def exchange(sock: socket.socket, opcode: int, payload: bytes, sequence: int = 1) -> tuple[int, bytes]:
    if not 1 <= sequence <= 0xffffffff:
        raise ValueError("tuner sequence must be 1..4294967295")
    request_sequence = sequence
    sock.sendall(struct.pack("<IHHII", MAGIC, VERSION, opcode, request_sequence, len(payload)))
    # KOS's BBA receive path is reliable with bounded writes; sending a header
    # plus a multi-KiB body in one host write can leave its blocking recv
    # waiting for a body it never observes.
    for offset in range(0, len(payload), TCP_WRITE_BYTES):
        sock.sendall(payload[offset:offset + TCP_WRITE_BYTES])
    magic, version, response, response_sequence, size = struct.unpack("<IHHII", receive(sock, 16))
    if (magic, version, response, response_sequence) != (MAGIC, VERSION, opcode | 0x8000, request_sequence) or size < 4:
        raise RuntimeError("invalid tuner response")
    result, = struct.unpack("<i", receive(sock, 4))
    return result, receive(sock, size - 4)


class Session:
    """One logical tuner transaction, using one BBA connection per command."""
    def __init__(self, host: str, payload_bytes: int = 0):
        self.host = host

    def request(self, opcode: int, payload: bytes = b"") -> tuple[int, bytes]:
        return request(self.host, opcode, payload)

    def __enter__(self): return self
    def __exit__(self, *_): return False


def session_upload(session: Session, payload: bytes) -> tuple[int, bytes]:
    return upload_asset(session.host, payload)


def request(host: str, opcode: int, payload: bytes = b"") -> tuple[int, bytes]:
    # Only observation requests are safe to repeat after a broken connection:
    # the peer may already have performed PLAY/PATCH/EXIT before its reply died.
    attempts = 3 if opcode in (PING, STATUS, UPLOAD_BEGIN, UPLOAD_CHUNK) else 1
    timeout = max(30, 30 + len(payload) // 10000)
    for attempt in range(attempts):
        try:
            with socket.create_connection((host, PORT), timeout=10) as sock:
                sock.settimeout(timeout)
                return exchange(sock, opcode, payload)
        except (ConnectionError, socket.timeout) as error:
            if attempt + 1 == attempts:
                raise error
    raise AssertionError("unreachable")


def request_sequence(host: str, frames: list[tuple[int, bytes]], on_ack=None) -> tuple[int, bytes]:
    """Send one acknowledged frame per BBA connection.

    KOS does not reliably surface peer close while the server is blocked in
    recv, so each request is deliberately a complete connection lifecycle.
    """
    for index, (opcode, payload) in enumerate(frames):
        result, data = request(host, opcode, payload)
        if result:
            return result, data
        if on_ack:
            on_ack(index)
    return result, data


def validate_asset(payload: bytes) -> None:
    if len(payload) < AFX_HEADER_BYTES:
        raise ValueError(f"AFX file is truncated ({len(payload)} bytes; header is {AFX_HEADER_BYTES})")
    magic, abi, total_size = struct.unpack_from("<III", payload)
    if magic != AFX_MAGIC:
        raise ValueError("file is not an AFX asset")
    if abi != AFX_ABI:
        raise ValueError(f"AFX ABI {abi} is incompatible with tuner ABI {AFX_ABI}; recompile this project")
    from afx_metadata import read
    read(payload)
    if total_size != len(payload):
        raise ValueError(f"AFX header declares {total_size} bytes but file contains {len(payload)}")


def upload_staged(host: str, payload: bytes, commit: int,
                  progress: bool = False) -> tuple[int, bytes]:
    """Send a bounded upload transaction ending in the requested commit."""
    chunks = (len(payload) + CHUNK_BYTES - 1) // CHUNK_BYTES
    def report(index: int) -> None:
        if not progress:
            return
        sent = min(index * CHUNK_BYTES, len(payload))
        percent = sent * 100 // len(payload)
        filled = percent * 24 // 100
        print(f"\rupload [{'#' * filled}{'.' * (24 - filled)}] {percent:3}% {sent // 1024}/{(len(payload) + 1023) // 1024} KiB", end="", file=sys.stderr, flush=True)
        if sent == len(payload): print(file=sys.stderr, flush=True)
    if progress:
        print(f"upload [{'.' * 24}]   0% 0/{(len(payload) + 1023) // 1024} KiB", file=sys.stderr, flush=True)
    frames = [(UPLOAD_BEGIN, struct.pack("<I", len(payload)))]
    frames.extend((UPLOAD_CHUNK, struct.pack("<I", start) + payload[start:start + CHUNK_BYTES])
                  for start in range(0, len(payload), CHUNK_BYTES))
    frames.append((commit, b""))
    return request_sequence(host, frames, lambda index: report(index) if 1 <= index <= chunks else None)


def upload_asset(host: str, payload: bytes, progress: bool = False) -> tuple[int, bytes]:
    """Use bounded acknowledged frames when a song exceeds one safe TCP payload."""
    validate_asset(payload)
    if len(payload) <= CHUNK_BYTES:
        return request(host, UPLOAD, payload)
    return upload_staged(host, payload, UPLOAD_COMMIT, progress)


def validate_bank(payload: bytes) -> None:
    if len(payload) < AFB_HEADER_BYTES:
        raise ValueError("AFB file is truncated")
    magic, version, low, high, data_at, data_bytes, total, reserved = struct.unpack_from("<8I", payload)
    if (magic, version, total, reserved) != (AFB_MAGIC, AFB_VERSION, len(payload), 0) or not (low or high):
        raise ValueError("file is not an AFB sample bank")
    if data_at != AFB_HEADER_BYTES or data_bytes != len(payload) - data_at:
        raise ValueError("AFB payload range is invalid")


def validate_control(payload: bytes) -> None:
    validate_asset(payload)
    header = struct.unpack_from("<20I", payload)
    if not (header[10] or header[11]) or header[13] != header[9]:
        raise ValueError("AFX is not a bank-bound control flow")


def validate_seek_index(payload: bytes) -> None:
    if len(payload) < AFC_HEADER_BYTES:
        raise ValueError("AFC file is truncated")
    magic, version, control, low, high, data_at, data_bytes, total = struct.unpack_from("<8I", payload)
    if (magic, version, total) != (AFC_MAGIC, AFC_VERSION, len(payload)) or not control or not (low or high):
        raise ValueError("file is not an AFC seek index")
    if data_at != AFC_HEADER_BYTES or not data_bytes or data_bytes != len(payload) - data_at:
        raise ValueError("AFC payload range is invalid")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("ping", "bank-upload", "control-upload-play", "play", "region", "stop", "status", "gain", "patch", "lane-mute", "dsp-enable", "dsp-disable", "dsp-returns", "dsp-room", "asset-read", "message", "exit", "dsp-program", "dsp-program-ring", "dsp-readback", "register-read"))
    parser.add_argument("--host", default="10.0.0.184")
    parser.add_argument("--file", type=Path, help="AFX/AFB file for upload")
    parser.add_argument("--index", type=Path, help="optional AFC index attached after control upload")
    parser.add_argument("--gain", type=int, help="0..255 running-instance gain")
    parser.add_argument("--enabled", type=int, choices=(0, 1), help="0 or 1 for dsp-returns")
    parser.add_argument("--channel", type=int, help="local channel for patch")
    parser.add_argument("--mask", type=lambda value: int(value, 0), help="field mask for patch")
    parser.add_argument("--values", nargs="*", type=lambda value: int(value, 0), help="packed patch words")
    parser.add_argument("--offset", type=lambda value: int(value, 0), help="asset-read byte offset")
    parser.add_argument("--bytes", type=int, help="asset-read count (1..4096)")
    parser.add_argument("--start-ms", type=int, default=0)
    parser.add_argument("--duration-ms", type=int, default=8000)
    parser.add_argument("--text", help="message for the Dreamcast screen (1..79 ASCII characters)")
    parser.add_argument("--feedback", type=int, help="DSP room feedback (0..32760, aligned to 8)")
    parser.add_argument("--damping", type=int, help="DSP room damping (0..32000)")
    parser.add_argument("--wet", type=int, help="DSP room wet gain, Q8")
    parser.add_argument("--rbl", type=int, choices=range(4), help="DSP delay-ring RBL (0..3)")
    parser.add_argument("--diffuse", action="store_true")
    parser.add_argument("--large", action="store_true")
    args = parser.parse_args()
    payload = b""
    if args.command in ("bank-upload", "control-upload-play"):
        if not args.file: parser.error(f"{args.command} requires --file")
        payload = args.file.read_bytes()
        (validate_bank if args.command == "bank-upload" else validate_control)(payload)
        commit = BANK_COMMIT if args.command == "bank-upload" else CONTROL_COMMIT_PLAY
        result, _ = upload_staged(args.host, payload, commit, progress=True)
        if not result and args.index:
            index = args.index.read_bytes()
            validate_seek_index(index)
            result, _ = upload_staged(args.host, index, SEEK_INDEX_COMMIT_PLAY, progress=True)
        print(json.dumps({"result": result}))
        return 0 if result == 0 else 1
    elif args.command == "message":
        if args.text is None: parser.error("message requires --text")
        payload, opcode = message_payload(args.text), MESSAGE
    elif args.command == "region":
        if not 0 <= args.start_ms <= 0xffffffff or not 1 <= args.duration_ms <= 30000:
            parser.error("region requires --start-ms >= 0 and --duration-ms 1..30000")
        payload, opcode = struct.pack("<II", args.start_ms, args.duration_ms), PLAY_REGION
    elif args.command == "gain":
        if args.gain is None or not 0 <= args.gain <= 255: parser.error("gain requires --gain 0..255")
        payload, opcode = bytes((args.gain,)), INSTANCE_GAIN
    elif args.command == "patch":
        if args.channel is None or args.mask is None or args.values is None: parser.error("patch requires --channel --mask --values")
        if not 0 <= args.channel < 64 or args.mask < 0 or args.mask >= 1 << 17 or bin(args.mask).count("1") != len(args.values):
            parser.error("invalid patch channel, mask, or value count")
        payload, opcode = struct.pack("<B3xI", args.channel, args.mask) + struct.pack("<" + "H" * len(args.values), *args.values), PATCH
    elif args.command == "lane-mute":
        if args.mask is None or not 0 <= args.mask < 1 << 32:
            parser.error("lane-mute requires --mask 0..0xffffffff")
        payload, opcode = struct.pack("<I", args.mask), LANE_MUTE
    elif args.command in ("dsp-program", "dsp-program-ring"):
        if args.file is None: parser.error("dsp-program requires --file")
        payload = args.file.read_bytes()
        if len(payload) != 1412: parser.error("DSP program must contain exactly 1412 bytes")
        if args.command == "dsp-program-ring":
            if args.rbl is None: parser.error("dsp-program-ring requires --rbl 0..3")
            payload = bytes((args.rbl,)) + payload
            opcode = DSP_PROGRAM_RING
        else: opcode = DSP_PROGRAM
    elif args.command == "dsp-room":
        if args.feedback is None or args.damping is None or args.wet is None:
            parser.error("dsp-room requires --feedback --damping --wet")
        if not 0 <= args.feedback <= 32760 or args.feedback % 8 or not 0 <= args.damping <= 32000 or not 0 <= args.wet <= 32767:
            parser.error("invalid DSP room parameters")
        payload, opcode = struct.pack("<HHHBB", args.feedback, args.damping, args.wet,
                                      int(args.diffuse) | (int(args.large) << 1), 0), DSP_ROOM
    elif args.command == "dsp-returns":
        if args.enabled is None: parser.error("dsp-returns requires --enabled 0 or 1")
        payload, opcode = bytes((args.enabled,)), DSP_RETURNS
    elif args.command == "dsp-readback":
        opcode = DSP_PROGRAM
    elif args.command == "register-read":
        if args.offset is None or args.bytes is None or args.offset % 4 or args.bytes % 4 or not 4 <= args.bytes <= 1024:
            parser.error("register-read requires aligned --offset and --bytes (4..1024)")
        payload, opcode = struct.pack("<II", args.offset, args.bytes // 4), REGISTER_READ
    elif args.command == "asset-read":
        if args.offset is None or args.bytes is None or not 0 <= args.offset <= 0xffffffff or not 1 <= args.bytes <= 4096:
            parser.error("asset-read requires --offset and --bytes 1..4096")
        payload, opcode = struct.pack("<II", args.offset, args.bytes), ASSET_READ
    else:
        opcode = {"ping": PING, "play": PLAY, "stop": STOP, "status": STATUS, "dsp-enable": DSP_ENABLE,
                  "dsp-disable": DSP_DISABLE, "exit": EXIT}[args.command]
    result, data = request(args.host, opcode, payload)
    if args.command in ("dsp-program", "dsp-program-ring", "dsp-readback", "register-read") and result == 0:
        if args.command in ("dsp-program", "dsp-program-ring") and data != (payload[1:] if args.command == "dsp-program-ring" else payload):
            raise RuntimeError("DSP program readback differs from upload")
        print(json.dumps({"result": result, "bytes": len(data), "hex": data.hex()}))
    elif args.command == "ping" and result == 0:
        print(json.dumps({"result": result, "max_opcode": struct.unpack("<I", data)[0] if len(data) == 4 else 15}))
    elif args.command == "status" and result == 0:
        state, afx_result, position, detail, timer, heartbeat, asset = struct.unpack("<7I", data)
        print(json.dumps({"state": state, "result": afx_result, "position": position, "detail": detail,
                          "timer_ticks": timer, "heartbeat": heartbeat, "asset": asset}, sort_keys=True))
    elif args.command == "asset-read" and result == 0:
        print(json.dumps({"bytes": len(data), "hex": data.hex()}, sort_keys=True))
    else:
        print(json.dumps({"result": result}))
    return 0 if result == 0 else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ConnectionError, OSError, RuntimeError, ValueError, socket.timeout) as error:
        print(f"afx-tuner: {error}", file=sys.stderr)
        raise SystemExit(2)
