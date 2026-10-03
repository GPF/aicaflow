# Testing

`make check` builds the native C tools, runs host driver tests, sanitizer-backed
C authoring tests and deterministic Python reference/client checks. It does not
require an ARM7 toolchain or a game ROM. Use Clang with AddressSanitizer and
UndefinedBehaviorSanitizer support, zlib, and Python with `mido` and `sf2utils`:

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install mido sf2utils
make check
```

Python packages are test/reference dependencies, not dependencies of the C
authoring binaries or runtime. Tests include malformed assets, bank bindings,
lossless merging, compact NOTE/PATCH encodings, AFP transforms, CSeq parsing,
ALBank SFX lifetimes/quality selection, MultiPCM and the tuner client.

`make firmware-check` is the maintainer check: it rebuilds the ARM7 firmware and
compares its SHA-256 with `firmware/manifest.json`. A release must pass both.

Host checks prove asset/command properties, not audible equality on hardware.
Hardware smoke testing is manual: run quickstart, the DSP demo and tuner,
verify playback, bank replacement, STOP/recycle, seek and a runtime DSP program.
Record source, KOS and firmware revisions with the result. The classical
player's `make frame-test` exercises its real loader and exits after all three
pieces; its [README](../examples/music_player/README.md#hardware-frame-test)
also explains how to restore an interactive build afterwards.

Game-corpus comparisons are separate integration checks: the generic tests do
not distribute or extract game assets. See [Authoring parity](authoring-parity.md)
for the measured DKR corpus and the distinction between source semantics,
functional equivalence and file-byte identity.
