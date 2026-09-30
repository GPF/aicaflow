# AICAflow runtime contract

The SH-4 runtime accepts one on-disk playback model only:

- An **AFB** file owns one contiguous sample payload in one AICA allocation.
- An **AFX** file is ABI 7, contains no samples, and names exactly one AFB
  identity (or identity zero for a noise-only flow).

All values are little-endian. AFB has a fixed 32-byte header; AFX has a fixed
80-byte header. The precise layouts are in
[`aicaflow/bank.h`](sh4/include/aicaflow/bank.h) and
[`aicaflow/protocol.h`](include/aicaflow/protocol.h).

Load a bank with `afx_bank_load_file()` or `afx_bank_load_memory()`, then load
its flow with `afx_bank_flow_upload()`. The loader verifies the AFB identity,
every bank-relative relocation and every sample range before it resolves the
AICA addresses. It retains the bank until every dependent flow is freed.

The ARM7 never parses AFB/AFX files, resolves a sample or allocates sample
memory. It receives only already-resolved AICA register values and executes
NOTE, PATCH, KEYOFF, WAIT, END and controlled PARK commands.

`PARK` is only for a controlled flow. A finite sound must carry KEYOFF and END
even when its source sample loops. AICA envelopes, pitch, filter, LFO, DSP send
and dynamic SH-4 register changes remain normal AICA register commands; the
format simplification does not restrict them.

Seek checkpoints are optional SH-4-only `.afc` sidecars. An AFC names the
exact AFX control identity and bank identity; it is loaded only by programs
that offer seeking. Normal playback has no seek index requirement. `.afp` is an offline transform of the AFX control stream:
it preserves NOTE/KEYOFF timing and never changes sample bytes.

There is deliberately no loader for embedded-sample AFX files, compact control
files, alternate sample banks or per-sample runtime handles. Offline tools must
combine sample sources into one AFB before writing the AFX control stream.
