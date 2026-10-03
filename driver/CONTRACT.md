# AICAflow runtime contract

See [Runtime ABI](../docs/specs/runtime.md) for ownership and execution,
and [Assets and sidecars](../docs/specs/assets.md) for file layouts.

The headers define the numeric contract:

- [`include/aicaflow/protocol.h`](include/aicaflow/protocol.h) — ARM7/SH-4 ABI,
  control region and binary format constants.
- [`sh4/include/aicaflow/bank.h`](sh4/include/aicaflow/bank.h) — AFB/AFX loader
  and bank lifetime API.
