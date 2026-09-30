# AICAflow runtime contract

The maintained runtime contract is in
[`docs/specs/runtime.md`](../docs/specs/runtime.md), with file formats and
sidecars in [`docs/specs/assets.md`](../docs/specs/assets.md). Keeping the
human-readable contract there makes it available with the rest of the release
documentation rather than split between source and docs.

The numeric authority remains the checked-in headers:

- [`include/aicaflow/protocol.h`](include/aicaflow/protocol.h) — ARM7/SH-4 ABI,
  control region and binary format constants.
- [`sh4/include/aicaflow/bank.h`](sh4/include/aicaflow/bank.h) — AFB/AFX loader
  and bank lifetime API.
