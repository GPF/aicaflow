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

## Deterministic A/B: state hash and PMU (added after the first results)
Same mGBADC revision `32890b7dd` for both backends, interpreter (`MGBA_SH4_DYNAREC=OFF`), software video, `DangerousXmas.gba`.

**State hash (cold-boot trail, project's own `tools/dc-coldboot` method):** built with `MGBA_DC_REPLAY_HARNESS/TRAIL`, hash every 8
frames over 480 frames from reset. Four hardware runs (KOS x2, AICAflow x2): all four produce **60 identical trail lines, and all are
identical to the checked-in baseline `tools/dc-coldboot/dxmas-f480-e8.trail`**. The transport swap leaves emulated state unchanged
over that workload. Speed per 120-frame window was also very repeatable: KOS 25.5 / 23.1 / 30.7 fps, AICAflow 27.4 / 24.3 / 33.9
(+7%, +5%, +11%); repeats differ by < 0.2 fps.

**PMU campaign (`docs/NATIVE_AUDIO_PMU_CAMPAIGN.md` method):** checkpoint `checkpoint-audio-dxmas-s2.state` + `input-audio-dxmas-s2.bin`
(staged under unique names), 120 warm-up frames then three 1,200-frame passes (cycles/instructions, I-cache, D-cache), audio ON and
`audioSync` ON, three clean hardware runs per backend (one KOS run was voided because the controller exit chord was used mid-run, and
re-run). Counters are system level (both frontend threads and interrupts included). Per frame:

| metric (mean of 3) | KOS snd_stream | AICAflow ring | AICAflow vs KOS |
|---|---|---|---|
| elapsed (3,600 frames) | 244.84 s | 234.45 s | -4.25% |
| fps | 14.70 | 15.36 | +4.44% |
| CPU cycles | 11,098,244 | 10,568,471 | **-4.77%** |
| instructions | 4,840,836 | 4,892,237 | +1.06% |
| I-cache misses | 209,262 | 152,063 | -27.3% |
| I-cache freeze cycles | 4,958,000 | 3,986,535 | -19.6% |
| D-cache misses | 68,176 | 94,529 | +38.7% |
| D-cache freeze cycles | 2,572,878 | 3,099,124 | +20.5% |

Run-to-run spread within a backend is < 0.02% on every counter, so the differences are systematic for this workload. The net saving
(~0.53 M cycles/frame) is a larger I-cache saving (-0.97 M freeze cycles) partly offset by more D-cache stalls (+0.53 M). Causes are
NOT established; plausible but unverified: the KOS stream path's extra thread/ARM7 traffic touches more code, and the AICAflow path
copies through host staging buffers.

**Ring health over the 235 s PMU runs (3 runs):** 5,062 rounds each (21.5/s), 0 late refills, 0 underruns, 0 generation / upload /
instance errors, minimum margin 1165-1171 frames, lateness <= 883 frames; service busy 4.9-5.7% of wall (cursor 0.46%, drain ~2.2%,
upload ~2.95%). The scene supplied only ~534 of 2,048 samples per round on average (about 26% of real-time audio), so 99.9% of rounds
were zero-padded, exactly as the KOS callback does.

**Caveat to keep front and center:** with audio production at 26-43% of real time, the ring mostly transports zeros. This proves
integration correctness, scheduling stability and cost under real mGBA load, NOT sustained full-rate audio delivery.

## Not established
- Behaviour when the emulator is at or above real time (every run so far was producer-starved), the dynarec build, SuperMarioAdvance1.
- Pause / resume / reset / state load, a long soak, the `MGBA_DC_AUDIO_AICAFLOW_TEST_TONE` diagnostic, line-out capture.
- Why the AICAflow path costs fewer cycles (needs a targeted measurement, e.g. the `BREAKDOWN`/`DCACHE_SCOPE` scopes, which this
  backend does not implement).
- Nothing about mGBADC is pushed (its remote is another user's repository); aicaflow `main` is pushed and the submodule pin
  `8007dba` is reachable on it.
