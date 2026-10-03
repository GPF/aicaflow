# AICAflow specifications

These documents describe the supported release contract. They replace old
design notes that mentioned embedded-sample AFX files, general chunk
containers or alternate runtime bank models.

- [Runtime ABI](runtime.md) — firmware bootstrap, commands and ownership.
- [Assets and sidecars](assets.md) — AFB, AFX, AFC, AFV, AFP, AFBM and the
  optional SH4 AFI catalog.
- [SFX bank maps](afsfx.md) — offline AFSFX purpose and the current DKR
  application reader; not another SH4/ARM7 file ABI.
- [AFX instruction language](instruction-language.md) — the exact timed
  bytecode in an AFX control stream.
- [SH-4 ↔ ARM7 IPC](ipc.md) — queue records, command ownership and durable
  observations.

Binary asset and IPC multibyte values are little-endian. AFP is JSON, and
AFBM/AFSFX are text; they are not uploaded to AICA. The exact numeric
constants and C layouts are authoritative in
[`driver/include/aicaflow/protocol.h`](../../driver/include/aicaflow/protocol.h)
and the validator in `driver/common/codec.c`.
