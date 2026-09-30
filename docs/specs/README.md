# AICAflow specifications

These documents describe the supported release contract. They replace old
design notes that mentioned embedded-sample AFX files, general chunk
containers or alternate runtime bank models.

- [Runtime ABI](runtime.md) — firmware bootstrap, commands and ownership.
- [Assets and sidecars](assets.md) — AFB, AFX, AFC, AFV, AFP, AFBM and the
  reserved AFI index.

All multibyte on-disk and wire values are little-endian. The exact numeric
constants and C layouts are authoritative in
[`driver/include/aicaflow/protocol.h`](../../driver/include/aicaflow/protocol.h)
and the validator in `driver/common/codec.c`.
