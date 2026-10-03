# AICA ring phase 1: afx_mem_upload() bandwidth sweep

## Goal
Measure real SH-4 -> AICA RAM write throughput of `afx_mem_upload()` while a looping
AICAflow voice plays, and check that the writes do not visibly disturb playback. Phase 1
of a mono PCM16 ring experiment. No refill loop, no underrun test, no stereo.

## Path under test (source)
`afx_mem_upload()` (`driver/sh4/src/allocator.c`) -> `upload_words()`
(`driver/sh4/src/dma.c:35`) -> KOS `spu_memload()`: a CPU copy over G2, **not DMA**. The
target must be 4-byte aligned and inside an existing allocation; a loaded bank's payload
(`afx_asset_addr(bank.asset)`) qualifies. RAM read-back confirmed the bank payload sits
at `afx_asset_addr()` and that the voice's sample base (relocation offset 0) is that address.

## Method (`examples/aica_ring_test`)
Voice on physical channel 0 (fresh AFX, as in the cursor probe), 4096-frame sine ring
(40 integer cycles, 430.7 Hz) + 4 guard frames. For each size in {512, 1024, 2048, 4096}
frames: 24 isolated uploads ~10 ms apart (cursor read before/after each), then 4 bursts of
16 back-to-back uploads. Cursor via the proven w32 select + 50 us settle under `g2_lock()`.
Each upload writes the same sine data to ring offset 0, so audio is steady. Region is read
back and compared after each size.

## Hardware result (real Dreamcast, 2026-10-03, one run)

| frames | bytes | upload us min/avg/max | KB/s (avg) | burst sustained KB/s (4x16) | headroom vs 88.2 |
|---|---|---|---|---|---|
| 512 | 1024 | 169 / 171.0 / 173 | 5987 | 5926-6046 | 68x |
| 1024 | 2048 | 339 / 341.2 / 344 | 6003 | 5979-6054 | 68x |
| 2048 | 4096 | 684 / 685.7 / 689 | 5973 | 6006-6022 | 68x |
| 4096 | 8192 | 1362 / 1369.5 / 1380 | 5982 | 5999-6021 | 68x |

(KB = 1000 bytes.) Linear fit: 0.1672 us/byte, fixed overhead about 0 us; predicted vs
measured within 1 us at every size. Worst single call across 96+ isolated and 256 burst
calls was 1421 us (a 4096-frame call). Read-back verify OK at every size. Instance stayed
PARKED, no failed uploads.

Cursor vs wall time (44.1 samples/ms), measured around each isolated upload and each
burst: worst deviation 2.7 samples, within the ~+-3 sample read resolution. Even a 22 ms
burst of continuous G2 writes did not slow the cursor. By ear: **NOT YET REPORTED**.

## Interpretation
- Throughput is about 6.0 MB/s, roughly 68x the 88.2 KB/s mono PCM16 44.1 kHz needs
  (34x for stereo). A half-ring refill of 2048 frames takes about 0.69 ms against a
  46 ms window (about 1.5% of one SH-4 core for mono, about 3% for stereo, since the
  copy is CPU-driven).
- Upload time is linear in bytes with tiny jitter, so refill cost is predictable.
- SH-4 G2 writes of this size did not disturb AICA playback timing.

## Not established
- Streaming viability: no refill loop, cursor-driven scheduling, boundary continuity, or
  underrun detection yet.
- Contention under real load (mGBA emulation, video, GD-ROM, other G2 users). This ran
  on an otherwise idle SH-4.
- Audibility/clicks; interaction of guard-frame refresh with a live ring.
- Stereo, ARM7 refill service, API shape, channel discovery beyond the fresh-AFX assumption.
- Statistical depth: one run.

## Next (per plan)
Mode A: 60 s refill loop with a phase-continuous sine and generation tracking, then
Mode B and a deliberate underrun, then stereo/phase-lock questions.

## Errata (KOS timer, found later)
`timer_us_gettime64()` in KOS runs 0.25% slow and steps forward ~2.5 ms once per real second (TMU2 ticks are converted at 80 ns but are really 80.2 ns; see `aica-ring-mode-a.md`). Upload times and KB/s here were measured with it: the ~0.25% slope error is negligible, but an occasional single call looks 2.5 ms longer when a second boundary falls inside it (seen once as a 3862 us outlier with a matching -110 frame cursor deviation). It was a clock step, not a bus stall or an AICA freeze. Bandwidth conclusions stand. The example now uses a corrected clock.
