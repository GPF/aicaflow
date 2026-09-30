#!/usr/bin/env python3
"""Small transport-free check for bounded tuner upload framing."""

import client
import struct
import subprocess
import sys
import tempfile
from pathlib import Path



calls = []
real_request_sequence = client.request_sequence
client.request_sequence = lambda host, frames, on_ack=None: (calls.extend((host, opcode, payload) for opcode, payload in frames) or (0, b""))
payload = bytearray(client.CHUNK_BYTES * 3)
struct.pack_into("<III", payload, 0, client.AFX_MAGIC, client.AFX_ABI, len(payload))
payload = bytes(payload)
assert client.upload_asset("dreamcast", payload) == (0, b"")
assert calls[0] == ("dreamcast", client.UPLOAD_BEGIN, len(payload).to_bytes(4, "little"))
assert [opcode for _, opcode, _ in calls[1:-1]] == [client.UPLOAD_CHUNK] * 3
assert [int.from_bytes(payload[:4], "little") for _, _, payload in calls[1:-1]] == [0, client.CHUNK_BYTES, client.CHUNK_BYTES * 2]
assert b"".join(payload[4:] for _, _, payload in calls[1:-1]) == payload
assert calls[-1] == ("dreamcast", client.UPLOAD_COMMIT, b"")
calls.clear()
bank = struct.pack("<8I", client.AFB_MAGIC, client.AFB_VERSION, 1, 0,
                   client.AFB_HEADER_BYTES, client.CHUNK_BYTES,
                   client.AFB_HEADER_BYTES + client.CHUNK_BYTES, 0) + bytes(client.CHUNK_BYTES)
client.validate_bank(bank)
assert client.upload_staged("dreamcast", bank, client.BANK_COMMIT) == (0, b"")
assert calls[-1] == ("dreamcast", client.BANK_COMMIT, b"")
seek = struct.pack("<8I", client.AFC_MAGIC, client.AFC_VERSION, 1, 1, 0,
                   client.AFC_HEADER_BYTES, client.CHUNK_BYTES,
                   client.AFC_HEADER_BYTES + client.CHUNK_BYTES) + bytes(client.CHUNK_BYTES)
client.validate_seek_index(seek)
client.request_sequence = real_request_sequence
for invalid, expected in ((b"", "truncated"),
                          (struct.pack("<III", client.AFX_MAGIC, 2, 80) + bytes(68), "ABI 2"),
                          (struct.pack("<III", client.AFX_MAGIC, client.AFX_ABI, 81) + bytes(68), "declares")):
    try:
        client.validate_asset(invalid)
    except ValueError as error:
        assert expected in str(error)
    else:
        raise AssertionError("accepted invalid AFX header")
with tempfile.TemporaryDirectory() as temporary:
    old_abi = Path(temporary) / "old.afx"
    old_abi.write_bytes(struct.pack("<III", client.AFX_MAGIC, 2, 80) + bytes(68))
    rejected = subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] / "tuner/client.py"),
                               "control-upload-play", "--file", str(old_abi)], text=True, capture_output=True)
    assert rejected.returncode == 2 and "ABI 2 is incompatible" in rejected.stderr


class ResponseSocket:
    def __init__(self): self.response = b""; self.sent = b""
    def sendall(self, data):
        self.sent = data
        _, _, opcode, sequence, _ = struct.unpack("<IHHII", data[:16])
        self.response = struct.pack("<IHHIIi", client.MAGIC, client.VERSION, opcode | 0x8000, sequence, 4, 0)
    def recv(self, size):
        result, self.response = self.response[:size], self.response[size:]
        return result


wire = ResponseSocket()
assert client.exchange(wire, client.PING, b"", 7) == (0, b"")
assert struct.unpack("<IHHII", wire.sent[:16])[3] == 7


class PayloadSocket:
    def __init__(self): self.sent, self.response, self.expected = [], b"", 0
    def sendall(self, data):
        self.sent.append(data)
        if len(self.sent) == 1:
            _, _, opcode, sequence, self.expected = struct.unpack("<IHHII", data)
            self.opcode, self.sequence = opcode, sequence
        elif sum(len(part) for part in self.sent[1:]) == self.expected:
            self.response = struct.pack("<IHHIIi", client.MAGIC, client.VERSION,
                                        self.opcode | 0x8000, self.sequence, 4, 0)
    def recv(self, size):
        result, self.response = self.response[:size], self.response[size:]
        return result


payload_wire = PayloadSocket()
assert client.exchange(payload_wire, client.UPLOAD, b"x" * 2049, 3) == (0, b"")
assert [len(part) for part in payload_wire.sent] == [16, 1024, 1024, 1]
wire.sendall = lambda _data: setattr(wire, "response", struct.pack("<IHHIIi", client.MAGIC, client.VERSION,
                                                                      client.PING | 0x8000, 8, 4, 0))
try:
    client.exchange(wire, client.PING, b"", 7)
except RuntimeError:
    pass
else:
    raise AssertionError("accepted a mismatched response sequence")
try:
    client.exchange(wire, client.PING, b"", 0)
except ValueError:
    pass
else:
    raise AssertionError("accepted sequence zero")


class Socket:
    def __enter__(self): return self
    def __exit__(self, *_): return False
    def settimeout(self, _): pass


connections = []
client.socket.create_connection = lambda *_args, **_kwargs: (connections.append(1) or Socket())
client.exchange = lambda *_args: (_ for _ in ()).throw(client.socket.timeout())
for opcode, expected in ((client.PING, 3), (client.STATUS, 3), (client.PLAY, 1),
                         (client.PATCH, 1), (client.UPLOAD_BEGIN, 3),
                         (client.UPLOAD_CHUNK, 3), (client.EXIT, 1)):
    connections.clear()
    try:
        client.request("dreamcast", opcode)
    except client.socket.timeout:
        pass
    else:
        raise AssertionError(f"{opcode} unexpectedly succeeded")
    assert len(connections) == expected

requests = []
client.request = lambda _host, opcode, payload=b"": (requests.append((opcode, payload)) or (0, b""))
client.request_sequence("dreamcast", [(client.UPLOAD_BEGIN, b"x"), (client.UPLOAD_COMMIT, b"")])
assert requests == [(client.UPLOAD_BEGIN, b"x"), (client.UPLOAD_COMMIT, b"")]
with client.Session("dreamcast") as session:
    assert session.request(client.PING) == (0, b"")
assert requests[-1] == (client.PING, b"")
assert (client.DSP_ENABLE, client.DSP_DISABLE) == (13, 14)
assert client.PLAY_REGION == 16
assert (client.BANK_COMMIT, client.CONTROL_COMMIT_PLAY, client.DSP_ROOM, client.LANE_MUTE, client.DSP_RETURNS,
        client.SEEK_INDEX_COMMIT_PLAY) == (20, 21, 22, 23, 24, 26)
for invalid, validator in ((b"", client.validate_bank), (b"AFB0" + bytes(32), client.validate_bank),
                           (b"", client.validate_control), (b"AFC0" + bytes(76), client.validate_control),
                           (b"", client.validate_seek_index), (b"AFC0" + bytes(32), client.validate_seek_index)):
    try:
        validator(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError("accepted invalid bank/control header")
old_argv = sys.argv
sys.argv = ["afx_tuner_client", "region", "--start-ms", "12000", "--duration-ms", "8000"]
try:
    assert client.main() == 0
finally:
    sys.argv = old_argv
assert requests[-1] == (client.PLAY_REGION, struct.pack("<II", 12000, 8000))

print("tuner chunk framing checks passed")

assert client.MESSAGE == 17
assert client.message_payload("Afspiller Monkey Island Theme") == b"Afspiller Monkey Island Theme"
assert len(client.message_payload("x" * 79)) == 79
for bad in ("", "x" * 80, "a\nb", "a\0b", "\x7f", "ø"):
    try:
        client.message_payload(bad)
    except ValueError:
        pass
    else:
        raise AssertionError("unsafe screen message accepted")
