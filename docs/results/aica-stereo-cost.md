# Production-like stereo ring: SH-4 cost

## Goal
`aica-stereo-ring.md` passed but reported 16.5% SH-4 busy, inflated by test diagnostics. Measure the cost of the same
stereo ring with a lean hot path, split into buckets, before putting it under mGBA load.

## Method (`examples/aica_stereo_cost_test`)
- Same logic as the validated stereo ring: both voices from one flow, per-start integer offset compensation (right ring is a
  rotation of the pattern), refills for both rings scheduled from the LEFT cursor only (right = left + offset), 2048-frame halves,
  guard frames mirrored when half 0 is refilled, stock AICAflow firmware.
- Removed from the steady state: per-sample test signal generation (pattern is precomputed with period = one half ring so a
  refill is a plain `afx_mem_upload`), read-back verification, the per-poll L,R,L validation triplet. Remaining per poll: one
  cursor read, unwrap, margin/generation bookkeeping, refills when due, `afx_update` + instance status about every 25 ms.
- Verification moved outside the measured loop: the offset is re-measured at the end (must be within 0.5 sample of the start
  value) and both rings plus guards are compared with the expected content.
- Cost = work time only. The wait for the next poll is idle and not counted. Timing uses integer TMU2 ticks
  (`__dreamcast_get_ticks`, ~12.47 M ticks/s) to keep the instrumentation cheap; "other" includes the timer calls' own overhead.
- Runs: 60 s at a 5 ms poll (the main number), then 20 s each at 10 ms and 20 ms polls.

## Hardware result (real Dreamcast, 2026-10-03, one run)
| poll | busy (wall %) | cursor | upload | status | other | min margin L/R | max lateness |
|---|---|---|---|---|---|---|---|
| 5 ms (60 s) | **4.40%** | 1.17% | 2.96% | 0.16% | 0.12% | 1828 / 1828 | 220 |
| 10 ms (20 s) | 3.78% | 0.59% | 2.95% | 0.18% | 0.07% | 1608 / 1609 | 440 |
| 20 ms (20 s) | 3.45% | 0.29% | 2.95% | 0.17% | 0.04% | 1168 / 1167 | 880 |

Per event (5 ms run): cursor read avg 58.5 us (max 121), upload of one 4096-byte half plus guard avg 687 us (max 752),
average poll 220 us (max 1547 us when both channels refill in the same poll).

All three runs: 0 late refills, 0 underruns, 0 generation errors, 0 upload errors, 0 cursor anomalies, 0 instance errors,
offset change over the run <= 0.013 sample, rings intact. (Late limit scaled to the poll interval; danger margin 1024.)
Calibrated start offsets: -19.03, -18.00, -19.02 samples (one-sample start jitter again).

## Interpretation
- A production-like stereo ring costs about **4.4% of the SH-4 at a 5 ms poll**, 3.8% at 10 ms. The floor is the CPU copy over
  G2 (about 3.0%: 687 us x 2 channels x 21.5 halves/s). Cursor polling is the only component that scales with poll rate
  (58.5 us per read, of which 50 us is the deliberate settle spin).
- 20 ms polling still held (min margin 1167) but only 143 frames above the danger threshold. A larger ring (for example
  4096-frame halves) would buy margin for coarse polling at the cost of AICA RAM.

## Not established / caveats
- Idle SH-4 only: no mGBA, video, GD-ROM or SD traffic, no thread wake-up cost (this loop spins between polls).
- Worst-case blocking is ~1.55 ms per poll; splitting the two channels' uploads across polls would halve it (not tried).
- Possible reduction of the 3% copy floor via DMA uploads (not tried; `afx_mem_upload` is a CPU copy).
- The stream is a periodic pattern, so this run cannot detect a mis-timed refill by content; timing correctness rests on the
  same cursor/margin/generation accounting validated in `aica-ring-mode-a.md`, `aica-ring-underrun.md` and `aica-stereo-ring.md`.
- One console, one run; instrumentation overhead (a few timer reads per poll) is included in "other".
