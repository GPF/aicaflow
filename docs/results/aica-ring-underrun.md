# AICA ring Mode U: deliberate underrun and recovery

## Goal
After Mode A (`aica-ring-mode-a.md`), answer two questions for the SH-4-managed mono ring:
1. Can we detect an underrun before/when the cursor enters stale data?
2. Can the stream recover deterministically afterwards?

## Method (`examples/aica_ring_test`, Mode U = `run_ring(..., inject=true)`)
- Normal streaming (identical loop to Mode A) for 12 s.
- Arm: at the first refill due after 12 s (stream half K), withhold that refill. Ring half K&1
  keeps stream half K-2, so the cursor will re-enter stale data.
- Early warning: `UNDERRUN_WARNING` when valid frames ahead of the playhead (`prod - play_abs`)
  fall to <= 512 while a refill is withheld (stale data not yet reached).
- Detection: `margin <= 0` with the cursor in half K, plus generation mismatch
  (`expected_gen = cursor half`, `actual_gen = gen[ring half]`).
- Recovery: on detection, perform the withheld refill immediately, then the catch-up refill of K+1;
  `RECOVERY_OK` after 3 further refills with margin >= 1024 and no underrun/generation error.
  Then 12 s more of normal streaming.
- Pass: exactly 1 intentional underrun, warning fired before stale data, 0 unexpected underruns /
  generation errors / late refills / guard / verify / cursor / instance errors, normal-window min
  margin >= 1024, recovery OK, >= 11.8 s of clean post-recovery streaming.

## Hardware result (real Dreamcast, 2026-10-03, one run)
```
UNDERRUN_ARMED generation=260 half=0
REFILL_SKIPPED half=0 generation=260 lateness_frames=56 margin=1992
UNDERRUN_WARNING margin=448 cursor_frames=3648 ring_half=1 pending_generation=260 (stale data not yet reached)
UNDERRUN cursor=215 half=0 expected_gen=260 actual_gen=258 detect_latency_frames=215 warned_before=1
RECOVERY_REFILL ... K=260 ... margin_after=1833
RECOVERY_REFILL ... K=261 ... margin_after=3881
RECOVERY_OK refills_after_recovery=3 margin=4071 generation_resynced=1
```
| metric | value |
|---|---|
| intentional underruns / generation errors | 1 / 1 |
| unexpected underruns / generation errors | 0 / 0 |
| warning before stale data | yes, at margin 448 (11 ms ahead) |
| detection latency after the cursor entered stale data | 215 frames (4.9 ms; one poll) |
| late refills / guard mismatches / verify / cursor / instance errors | 0 / 0 / 0 / 0 / 0 |
| min margin outside the episode / during it | 1828 / -215 |
| post-recovery clean streaming | 12.0 s |
| SH-4 busy | 3.4% |

`MODE_U_PASS`. (`max_lateness` 2263 in the summary is just the deliberate withholding.)

## What this shows / does not show
Shows: with the cursor monitor and host-side generation bookkeeping, an underrun is detected at
the expected half and generation, can be predicted ~11 ms ahead from the valid-frame margin, and
the stream recovers with one immediate refill plus a catch-up refill, with bookkeeping
resynchronised and no AFX/runtime failure.

Does not show: audibility of the glitch (expected: the stale half replays old audio for up to one poll,
about 5 ms, before the recovery refill); behaviour of an underrun caused by a real stall under
emulator load rather than a withheld refill; stalls longer than the ring (then `play_abs` unwrap is
ambiguous); stereo / two-voice phase lock; any mitigation such as muting on warning; more than one
injected event per run; runs with video/GD-ROM/emulation load.
