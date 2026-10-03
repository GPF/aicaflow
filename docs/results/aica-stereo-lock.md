# Stereo phase lock: do two voices started by one flow stay locked?

## Goal
A stereo SH-4-managed ring uses two looping voices (left and right). Before adding refill logic, answer:
do two voices started by one AFX flow stay sample-phase locked, how large is their start offset,
and does it move over time or under ring-upload load?

## Method (`examples/aica_stereo_lock_test`)
- One flow, 2 channels, 2 setups (each its own 4096-frame ring in one bank), `NOTE_PL` for channel 0 then
  channel 1 at tick 0, then `PARK`. Left: hard left (DIPAN 0x1f), 440 Hz-ish (40 cycles/ring = 430.7 Hz).
  Right: hard right (DIPAN 0x0f), 646 Hz (60 cycles). Physical channels from the channel map: L=0, R=1.
- Offset = R cursor - L cursor in samples. Read L, R, L (w32 select + 50 us settle each, see
  `aica-cursor-probe.md`) and interpolate L to the time of the R read, using the corrected clock
  (`now_us()`; KOS `timer_us_gettime64()` steps +2.5 ms each second). About 8000 triplets per 1.5 s.
- Bias control: the same channel in both roles must read ~0.
- Phases: 6 independent starts; 20 s steady (1 s windows); 20 s with continuous ring uploads
  (2 x 4096 B every 46 ms, the stereo refill cadence; same data, so audio is unchanged).

## Hardware result (real Dreamcast, 2026-10-03, two runs; second run shown)
| measurement | result |
|---|---|
| bias control (same channel as L and R) | mean -0.008, sd 0.43 samples (PASS) |
| start offset, 6 starts (R - L) | -19.02, -18.03, -18.02, -18.03, -18.02, -19.02 samples |
| start-to-start spread | 1.003 samples = one sample-clock quantum (22.8 us) |
| 20 s steady, worst 1 s window vs first | 0.017 sample (sd 0.43-0.49, quantisation) |
| 20 s with ring uploads (862 uploads), worst window | 0.013 sample; 0 upload failures; instance stayed PARKED |

The first run (6 starts) gave -19.01 x3 then -18.01 x3: same two values.

## Findings
1. **Rate lock: yes.** Both voices run off the same sample clock; over 20 s the offset moves by < 0.02 sample,
   with or without upload load. No drift to track.
2. **Start offset: not zero.** The right voice's cursor lags the left by **18-19 samples (about 0.41-0.43 ms)**,
   consistently, with +-1 sample (one clock quantum) start-to-start jitter. Likely cause (my inference from the
   magnitude and the code, not measured): the ARM7 executes the channel-0 and channel-1 `NOTE_PL` events one after
   the other, and each programs a full set of registers, so key-on for channel 1 lands ~19 sample periods later.
3. A 0.42 ms inter-channel delay is a real interaural-time-difference cue, so uncompensated it would pull a
   centre image slightly toward the left voice. It is constant within a start, so it can be compensated.

## Implication for a stereo ring
- One cursor read can drive both voices: the other cursor is that value minus the offset.
- Measure the offset once after each start (an L,R,L read burst of ~10 ms gives it to ~0.1 sample) and
  store the right ring shifted by that many samples (right ring index = left ring index - offset), or equivalently
  refill the right channel's halves offset boundaries. Do not assume a fixed 18 or 19: it changed by one sample
  between starts.
- Alternative (ARM7 change, out of scope here): program KEYONB for both channels and fire a single KYONEX so
  both start in the same sample. Not tried.

## Not established
- Behaviour with more than 2 channels, or with the voices started by separate flows/instances.
- Whether the offset changes with pitch, envelope or other setup fields (all fields were identical here).
- By ear: the left/right tone assignment and any audible image shift (not yet reported).
- Stereo refill itself (two rings, boundary handling with the offset, underrun detection per channel).
- Real emulator load; single hardware unit.
