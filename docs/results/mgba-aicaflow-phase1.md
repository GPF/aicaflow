# mGBADC with the AICAflow ring as the audio transport: Phase 1 first hardware results

## What was built
`mGBADC` branch `aicaflow-transport` (separate git worktree `~/code/dreamcast/mGBADC-aicaflow`, from `c0849c506`; the
`pvr-present` working tree with other uncommitted work was not touched). Design: `mgba-aicaflow-transport-design.md`.
- `src/platform/dreamcast/native/dc-audio-aicaflow.c`: same `dc_audio_init/shutdown` contract as `dc-audio.c`; drains the same
  planar blip buffers under the same `mCoreSyncLockAudio/ConsumeAudio` protocol, zero-pads shortfalls, and writes 2048-frame
  halves into two 4096-frame AICAflow rings (L/R voices). Start offset measured at init (L,R,L cursor bursts); the earlier voice
  is delayed causally by |offset| samples; the later voice's cursor is the single scheduling reference. Dedicated KOS thread,
  ~10 ms wakes, priority above the core. Stats line `AICAFLOW_AUDIO ...` every 5 s over serial.
- CMake option `MGBA_DC_AUDIO_AICAFLOW` (default OFF = KOS snd_stream). `third_party/aicaflow` is a git submodule pinned to
  aicaflow `8007dba` (URL recorded as the GitHub fork); the host library is built from it and `firmware/aicaflow.drv` is turned
  into a C array at build time by `cmake/embed_binary.cmake` (C11 has no `#embed`). Diagnostic option available in source:
  `-DMGBA_DC_AUDIO_AICAFLOW_TEST_TONE` (drain blip, play a synthetic tone) - not run yet.

## Test setup
Real console, `mgba-dc-native`, `DangerousXmas.gba`, **interpreter, no dynarec** (BUILDING.md default), `audioSync` on, exit via the
`/pc/mgba-exit` marker, 40 s per run, both backends built from the same revision. The emulator currently runs well below real
time in this configuration (STATUS.md: ~23-25 fps vs 59.7 native), so audio production is ~43% of real time for ANY backend.

## Results
**Ring health (AICAflow backend, 3 runs, 35 s of audio each):** 754 rounds (21.5/s = real-time cadence), 0 late refills,
0 underruns, 0 generation errors, 0 upload errors, 0 instance errors, minimum margin 1175-1262 frames (danger < 1024),
lateness <= 873 frames. Blip supply averaged ~860-1140 of 2048 samples per round, so ~99% of rounds were zero-padded exactly as
in the KOS callback (producer-limited, not a transport problem). Service busy time 5.5-9% of wall (drain 3.0-4.2%, upload
~3.0%, cursor 0.46%); wake gap avg 13.5 ms (KOS tick granularity), max 40-50 ms (includes the stats `printf`).

**Emulator speed, same revision, first 840 frames (7 windows of 120):**
| backend | run 1 | run 2 | run 3 | mean |
|---|---|---|---|---|
| AICAflow ring | 26.36 | 25.43 | 25.44 | 25.74 fps |
| KOS snd_stream | 24.94 | 24.71 | 24.24 | 24.63 fps |
The AICAflow build was not slower (about 4-5% faster, ranges not overlapping); the cause is unknown.

**By ear (user):** the AICAflow run sounded good, like previous PCM sound tests, and no audible difference was heard between the
AICAflow and KOS backends. Subjective, one listener, no line-out capture.

## First bug found by this test
The first version split a round over three wakes (drain, left upload, right upload). At KOS tick granularity (avg wake gap
14.6 ms) that capped throughput at ~21.2 rounds/s against 21.5 needed, so the ring fell steadily behind (margin to -6000,
~7 underruns/s) while the emulator was fine. Fixed by doing the whole round in one wake. The split-upload idea from the cost
test is not useful for a higher-priority service thread anyway.

## Not established
- State/replay hash and cycle (PMU) comparison on the deterministic replay workload; `audioSync` pacing could in principle
  change emulation timing. fps windows here are scene-dependent, not deterministic.
- Behaviour when the emulator is at/above real time (all runs were producer-starved), long soak, pause/resume/reset/state load.
- The test-tone diagnostic, a dynarec build, SuperMarioAdvance1, and a line-out capture.
- Nothing is pushed: the submodule pin `8007dba` exists only in the local aicaflow clone, so the mGBADC branch is not
  reproducible elsewhere until aicaflow is pushed.
