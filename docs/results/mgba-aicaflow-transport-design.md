# Design: AICAflow ring as the mGBADC audio transport (Phase 1, mixer unchanged)

Research/design only. Nothing in mGBADC was modified; facts below were read from its working tree on top of
`c0849c506` (`src/platform/dreamcast/native/dc-audio.c`, `src/core/sync.c`, `src/core/thread.c`, `src/gba/audio.c`,
`dc-main.c`). Hardware numbers come from the aicaflow results docs referenced inline.

## 1. What the current path does (facts)
- **Transport boundary.** `dc_audio_direct_cb(hnd, left, right, size_req)` (dc-audio.c:169) is the only place mixed PCM
  meets KOS. `left`/`right` are SPU-RAM P2 addresses of two separate per-channel buffers; the callback fills them and
  returns `size_req`. It is the same two-ring, planar model our stereo ring uses.
- **Geometry.** `snd_stream_init_ex(2, 8192)` => 8192 bytes = 4096 samples per channel ring (92.9 ms); `size_req` per
  call is at most half of that (2048 samples, 46 ms). Rate 44100.
- **Mixer interface is blip_t, untouched by the transport.** The callback does `getAudioChannel(0/1)`,
  `blip_set_rates(clockRate, 44100)`, `blip_samples_avail`, `blip_read_samples(..., stereo=0)` into host scratch
  (L and R separate, planar), then `memcpy` to the SPU addresses. PSG+FIFO mixing, SOUND_BIAS, clipping and resampling all
  happen before this point (see `aicaflow-gba-audio-feasibility.md` s2). A transport swap preserves them exactly.
- **Underrun policy today:** if fewer samples are available than requested, the remainder is zero-filled (dc-audio.c
  ~line 520-530) and `size_req` is still returned.
- **Sync contract (src/core/sync.c).** Consumer: `mCoreSyncLockAudio` (mutex) around `blip_set_rates/avail/read`, then
  `mCoreSyncConsumeAudio` (wake `audioRequiredCond` + unlock) BEFORE the slow SPU copy. Producer (core thread,
  gba/audio.c:630/646 in the audio sample path): after each batch, `mCoreSyncProduceAudio(sync, psg.left, audio->samples)`
  blocks while `audioWait && blip_samples_avail >= audio->samples`. `audio->samples` defaults to `GBA_AUDIO_SAMPLES = 2048`
  (dc-main does not override `audioBuffers`). With `audioSync = true` (dc-main.c:740/767) the emulator is therefore paced by
  how fast the consumer drains, and it blocks at 2048 buffered samples = exactly one current half buffer.
- **Execution context.** A dedicated KOS thread (`thd_create(0, ...)`, default priority) runs
  `snd_stream_poll(); thd_sleep(20)` (dc-audio.c:560). The core runs in mGBA's threaded run loop (`mCoreThreadStart`); the
  frontend loop presents video. Only dc-audio.c in `src/platform/dreamcast` touches KOS sound, so nothing else conflicts
  with replacing it (AICAflow's `afx_init` and KOS `snd_init` cannot coexist).
- **Build.** mGBADC is `CMAKE_C_STANDARD 11` (no `#embed`); AICAflow's firmware (`aicaflow.drv`) is 11,024 bytes, so it
  would be embedded as a generated array.

## 2. Phase 1 scope (as agreed)
```
existing mGBA mixer -> existing blip output -> planar PCM16 L/R -> NEW AICAflow ring backend -> AICA
```
Keep `dc_audio_init/shutdown` as the API. Replace only: `snd_stream_*` startup/poll, the SPU writes, and the "how much to
deliver" decision. No PSG offload, no change to blip, bias, clipping, resampling or the producer wait.

## 3. Proposed backend
1. **Init:** `afx_init(firmware)`, load a bank holding two rings (L, R), start both voices from one 2-channel flow
   (NOTE_PL ch0 then ch1, then PARK), as in `aica_stereo_ring_test`. Pan L=0x1f, R=0x0f. Pre-fill silence.
2. **Calibrate the start offset once per start** (L,R,L burst, ~0.2-1.5 s at init, before emulation is heavy):
   d = R-L cursor, 18-19 samples, +-1 between starts. Never hard-code it.
3. **Apply the offset by delaying the EARLY voice, not by reading ahead.** The experiments shifted the right ring forward
   in stream time, which needs 18 samples of lookahead - free for a synthetic stream, NOT available for a live blip buffer.
   Equivalent and lookahead-free: keep the later voice (R) chunk-aligned and store the earlier voice (L) delayed by |d|
   samples, using a small carry buffer of the last |d| samples of the previous L chunk (size it for 32). Cost: +0.4 ms
   latency. Use the later voice's cursor as the scheduling reference (or L + d; the lock test shows they track within 0.02
   sample).
4. **Service step (each wake):** read ONE cursor (50 us settle, 32-bit RMW select, see `aica-cursor-probe.md`), unwrap,
   compute per-channel margin/generation as in Mode A. For each half that the cursor has left (and that is not late):
   lock audio mutex -> `blip_set_rates` -> `avail = min(blip_samples_avail, half)` -> `blip_read_samples` L and R into
   scratch -> `mCoreSyncConsumeAudio` (unlock + wake producer) -> zero-pad any shortfall (today's policy) -> upload L half,
   then R half (+ guard frames when half 0), **in separate wakes if possible**.
5. **Chunking vs the producer limit.** The producer stops at 2048 buffered samples, so blip can never supply more than
   ~2048 (+ one batch) per drain. Keep halves at 2048 frames in Phase 1 (matches the producer, validated by all our
   experiments). A 4096-frame half could never be filled from one drain; if more slack is wanted, grow the RING
   (e.g. 8192 frames, still 2048-frame upload chunks written at a moving write pointer) rather than the half. That is a
   different, unvalidated scheme (wrap-split uploads, guard handling), so defer it unless margins demand it.
6. **Execution context:** reuse the dedicated thread, but wake every 10 ms (`thd_sleep(10)`, the KOS tick; the stereo cost
   test shows 3.8% busy and a 1608-frame minimum margin at 10 ms vs 1167 at the 20 ms the stream uses today) and give it
   higher priority than the core thread. Do not drive it from the frame loop: it would stall exactly when emulation
   overruns. Instrument max wake gap.
7. **Mute/pause/reset:** park cannot be used to silence a looping voice cheaply; patch TOTAL_LEVEL (MIX) to mute and refill
   silence. Define behavior for core reset / state load (flush blip, zero rings, keep generation/margins consistent).
8. **Clock:** use the corrected TMU2 tick clock (not `timer_us_gettime64`) for any pacing/instrumentation
   (`aica-ring-mode-a.md`).

## 4. How the cost findings apply
- 2 x 4 KB uploads back-to-back made one poll take up to 1.55 ms. Upload L and R halves in different wakes (or at least
  yield between them) to cap blocking near 0.75 ms.
- The ~3% floor is a CPU copy over G2. NOTE: KOS's current path also copies the same bytes with a plain `memcpy` to
  uncached SPU RAM, so the transport swap is not expected to add that floor; it may only add cursor polling (~0.3-1.2%) and
  the offset bookkeeping. This is a prediction, to be measured (see s6).
- Margin: at a 10 ms wake with 2048 halves, worst-case lateness ~440 frames leaves ~1600 frames of margin (idle SH-4).

## 5. Risks / unknowns, most important first
1. **Wake jitter under real load.** Every number so far is an idle SH-4 with a spinning loop. The poll thread competes with
   the core and video threads; KOS timeslice effects are unmeasured. Mitigation: priority, margin instrumentation.
2. **IRQ blackout from G2 locking - audited in KOS source (not hardware-measured).** `g2_lock()` = `irq_disable()` +
   suspend G2 DMA (SPU, BBA, CH2) + wait for the FIFOs (`g2bus.h`); masked IRQs also stop KOS preemption. But
   `spu_memload()` (what `afx_mem_upload` uses for aligned data) copies in 32-byte chunks: each does `g2_fifo_wait()` then
   `g2_write_block_32()`, which takes its own `g2_lock_scoped()` for that burst only (`hardware/spu.c`, `hardware/g2bus.c`).
   The measured 687 us per 4 KB is ~128 chunks at ~5.4 us, so IRQs are masked for single-digit microseconds at a time and
   the upload is preemptible at chunk granularity, NOT a 0.7 ms blackout. (`spu_memload_sq` does hold the lock for the whole
   store-queue copy; AICAflow does not use it.) The one real blackout is our own cursor read: the probe code wraps
   `g2_lock` around the 50 us settle spin (~60 us masked per poll). That lock is not needed during the spin (single user of
   the monitor, and a longer delay is harmless): take it for the select write, release, spin, retake for the read.
3. **Behavior at reset / state load / pause / underrun-recovery** is only partly exercised (underrun recovery was proven
   synthetically, `aica-ring-underrun.md`).
4. **Build integration:** linking `libaicaflow_host.a`, embedding the firmware without `#embed`, and keeping the audio PMU
   diagnostic build modes (`MGBA_DC_AUDIO_PMU_*`, ADPCM) working or explicitly excluded.
5. Single console; the KOS timer issue and 1002.27 Hz tick base are handled in aicaflow, but any mGBADC code using
   `timer_us_gettime64` for audio pacing carries the same 0.25% / 2.5 ms-per-second artifact.

## 6. Measurement plan for Phase 1 (before any PSG offload)
- A/B on the same deterministic replay workload mGBADC already uses for audio PMU work: KOS stream vs AICAflow ring,
  same ROM/checkpoint, audioSync on. Compare emulated frames/s and SH-4 cycles per frame; report the audio-attributed delta.
- Log in the service: min margin, late/underrun counts, max wake gap, per-bucket busy time (reuse `aica_stereo_cost_test`
  counters), periodically over serial.
- Listening check on a real game, then a long soak.
- Pass: no underruns/late refills at the target frame rate, cost not worse than the KOS stream within noise, audio identical
  in content (same blip data) apart from the 0.4 ms L/R offset compensation.

## 7. Decisions (Troy, 2026-10-03)
1. **Geometry:** keep 4096-frame rings with 2048-frame halves for Phase 1 (matches the producer's 2048-sample block and the
   existing KOS buffer, so the A/B comparison stays clean). If real-load testing needs more slack, test separately a larger
   physical ring with the same 2048-frame producer/upload chunks - a different buffering model needing its own evidence.
2. **Structure:** new `dc-audio-aicaflow.c` behind a CMake option (`MGBA_DC_AUDIO_AICAFLOW`, default OFF = current
   `dc-audio.c`/`snd_stream`), same public `dc_audio_init()/dc_audio_shutdown()` contract. The experiment is "feed the exact
   same PCM that goes to `snd_stream` into the proven AICAflow stereo ring": emulator state, mixer semantics, bias/clipping
   and audio production constant, transport the only variable.
3. **Dependency:** AICAflow as a pinned git submodule (`third_party/aicaflow`, from the GPF fork), not a sibling-path
   dependency and not copied sources. Firmware: generate the C array/object at BUILD time from
   `third_party/aicaflow/firmware/aicaflow.drv` (checked in upstream, so no ARM7 toolchain is needed); do not commit a
   hand-copied byte array. Generated files go to the build directory.
4. **Compensation:** delay the earlier (left) voice by |d| samples with a causal carry buffer; keep the startup calibration
   (the offset is 18 or 19 samples depending on the start).
5. **Service thread:** dedicated thread at 10 ms, higher priority than the core; split the L and R uploads across wakes to
   keep each wake short (the stall is CPU time, not an IRQ blackout - see s5 item 2); release `g2_lock` during the cursor
   settle spin.

## 8. Phase 1 acceptance (A/B on one mGBADC revision)
A = KOS `snd_stream`, B = AICAflow ring, same deterministic replay/workload. Compare: replay/state hash unchanged; zero audio
underruns; zero late ring refills; stable audio synchronisation; frame/runtime performance; SH-4 PMU/cost; audible output;
pause/resume/reset; line-out capture if needed. Only if B holds under real mGBA load is AICAflow viable as the Dreamcast PCM
transport; PSG offload is reopened only after that.
