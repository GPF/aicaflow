# Stereo ring with per-start host-side offset compensation

## Goal
After `aica-stereo-lock.md` (rate-locked voices, right starts 18-19 samples late with +-1 sample jitter), test a
stereo SH-4-managed ring that keeps stock firmware: measure the start offset per start, shift the right ring's
contents by it, and schedule both channels' refills from ONE cursor.

## Method (`examples/aica_stereo_ring_test`)
- Start both voices (one 2-channel flow, as in the lock test). Calibrate d = R - L over 1.5 s of L,R,L triplets
  (~8000), `d_int = round(d)`. Fill both rings (4096 frames + 4 guard each). Sync to a left-ring wrap.
- Right ring frame f holds stream sample `f - d_used` (left: f). So both channels output the same stream instant
  together. Halves of 2048 frames, same refill rule as `aica-ring-mode-a.md`, per channel.
- The left cursor is the only scheduling input: `play_R = play_L + d_int`. Every 5 ms poll reads an L,R,L triplet only
  to CHECK that premise (`d_now - d_cal`).
- Signal: loud click burst every 0.25 s, identical in both channels, plus quiet channel-identifying tones
  (L 440 Hz, R 646 Hz). Compensation alternates ON/OFF each 5 s (4 segments) so a listener can compare:
  ON should centre the clicks, OFF should pull the image toward the earlier (left) channel.
- 3 sessions, each restarted and recalibrated; per-channel underrun/generation/guard/verify accounting.

## Hardware result (real Dreamcast, 2026-10-03, one run)
| | session 1 | session 2 | session 3 |
|---|---|---|---|
| calibrated offset | -19.03 | -18.03 | -18.03 |
| compensation used | -19 | -18 | -18 |
| refills per channel (20.5 s) | 430 | 430 | 430 |
| underruns, late, generation, verify, guard (L and R) | 0 | 0 | 0 |
| min margin L / R (frames; danger < 1024) | 1828 / 1828 | 1828 / 1813 | 1828 / 1802 |
| max upload L / R (us, 4096 B) | 752 / 749 | 755 / 751 | 749 / 747 |
| offset deviation vs calibration: mean / sd / max abs | 0.009 / 0.44 / 1.51 | 0.006 / 0.44 / 1.97 | 0.001 / 0.44 / 1.97 |
| outliers beyond 3 samples | 0 | 0 | 0 |
| offset at session end vs start | +0.005 | -0.008 | +0.004 |
| cursor anomalies, instance errors | 0, 0 | 0, 0 | 0, 0 |

`STEREO_RING_PASS`. Recalibration spread across sessions was 1.003 samples, so a hard-coded 18 or 19 would have been
wrong in two of three sessions; the measured per-start value was right every time.

## By ear (user, 2026-10-03)
With compensation ON the clicks **sounded centred**. The OFF segments were not described. This is a single subjective
report from one listener on one setup; no line-out capture was made.

## What this establishes
- A single cursor can schedule a stereo ring: inferring the right cursor as left + d held to within the +-2 sample
  quantisation noise for 60 s in total, with live refills of both channels, and the offset did not drift.
- Host-side integer compensation needs no ARM7 change and needs only a ~1.5 s (or shorter) calibration burst per start.
- The right channel's margin is slightly lower than the left's (up to 26 frames) because it is refilled right after
  the left's upload; the minimum (1802) is still far above the 1024 danger threshold.

## Not established
- **OFF segments by ear.** Not reported, so the "OFF is pulled left" half of the A/B comparison is unconfirmed.
- CPU cost. `sh4_busy_pct` was 16.5% but that includes the per-poll L,R,L validation triplet, read-back verification and
  an expensive test-signal generator (64-bit modulo and `sinf` per frame); the production cost of a stereo ring is not
  isolated yet (mono was 3.5% with a cheaper generator).
- Compensation uses whole samples (fractional part up to 0.5 sample ~11 us is ignored); the +-1 sample start jitter is
  calibrated out only by measurement.
- No deliberate stereo underrun, no emulator load, a single console, and compensation was toggled live, which makes a
  small discontinuity at each switch.
- Not tried: an ARM7 change to start both channels in one sample (single key-on execute).
