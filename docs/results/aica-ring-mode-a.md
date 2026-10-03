# AICA ring Mode A: live mono refill (60 s)

## Goal
Prove a live SH-4-managed refill of a looping AICA ring: continuous 440 Hz sine, phase
carried across every refill, cursor polled every 5 ms, a half rewritten only after the
cursor has fully left it, guard frames mirrored whenever ring frames 0..3 change. Mono,
no deliberate underrun, no stereo, no mGBA load, no public API.

## Design (`examples/aica_ring_test`, Mode A)
- Ring: 4096 PCM16 frames + 4 guard frames in one AFB bank; one looping voice; halves of
  2048 frames (46.4 ms). Refills use `afx_mem_upload()` (CPU copy over G2).
- Producer counter `prod`, cursor unwrapped to `play_abs` from SH-4 cursor reads (w32 select +
  50 us settle under `g2_lock()`, see `aica-cursor-probe.md`). Stream half K goes to ring half
  K&1 once `play_abs >= (K-1)*2048`. `gen[half]` records which stream half each ring half holds;
  the cursor entering a half whose generation is wrong is a `generation_error`.
- Ring[0..3] changes (even K) are followed by a guard upload; every refill checks the guard
  equals ring[0..3] in AICA RAM, spot-checks 3 words of the half, and fully verifies the half
  for refills 1, 2 and every 128th.
- Pass requires: full duration, 0 underruns, 0 late refills (> 512 frames after the half
  became free), 0 generation errors, 0 cursor anomalies, 0 instance errors, 0 guard
  mismatches, 0 verify errors, minimum margin >= 1024 frames.

## Hardware result (real Dreamcast, 2026-10-03, final build)
60 000 ms, 12 000 polls, no poll stalls (max gap 5.06 ms).

| metric | value |
|---|---|
| refills | 1292 (expected ~1291) |
| min margin (valid frames ahead of playhead) | 1828 (danger < 1024) |
| max lateness (frames after half became free) | 220 (late > 512) |
| late refills / underruns / generation errors | 0 / 0 / 0 |
| guard updates / mismatches | 646 / 0 |
| verify errors / cursor errors / instance errors | 0 / 0 / 0 |
| upload avg / max | 685 us / 754 us (2048 frames = 4096 bytes) |
| SH-4 busy (cursor reads + generation + uploads + checks) | 3.5% |

`MODE_A_PASS`. By ear (user): steady tone in both ears, no ticks; the only audible change was
the expected 430.7 -> 440 Hz switch when Mode A overwrites the sweep's ring.

## The "once per second cursor anomaly" (a measurement artifact, now fixed)
First hardware runs failed on `cursor_errors=60`: exactly one poll per second showed the
cursor advancing ~110 frames instead of ~221, and the cursor stayed that far behind.
Investigation, in order:
1. Everything else passed, and the refill code did not matter: Flycast and Deecy reproduced the
   same 60 anomalies (so not BBA or real G2 contention), and variants with no refills, no
   `afx_update`, spin-polling and quiet logging (v1-v4) all still showed one per second.
2. A dense-read window (v5, Deecy) showed at the glitch a single read with `dt_us` ~2.5 ms but
   only ~2 frames of cursor advance (~50 us worth): the cursor was continuous, only the
   clock jumped (+2.52..2.57 ms in three consecutive windows).
3. Root cause in KOS (`kernel/arch/dreamcast/kernel/timer.c`, `include/arch/timer.h`):
   TMU2 reloads once per real second (`TIMER_PCK = 199499520/4`, so 12 468 720 ticks/s), but
   `arch_timer_gettime()` converts ticks at 80 ns each. The real tick is 80.2 ns, so
   `timer_us_gettime64()` advances only to ~997.5 ms per real second and then snaps +2.5024 ms
   when `secs` increments: the reading runs 0.25% slow and jumps forward 110 AICA frames
   every second. That is exactly the observed deficit, and it also explains the +0.25% "extra"
   frames on normal polls (221 vs 220.5).
4. Fix (test only): `now_us()` = `secs*1e6 + ticks * (1e6 / 12468720)` from
   `__dreamcast_get_ticks()`. With it, `cursor_errors=0` over 60 s on hardware and the
   AICA/clock ratio is 1.0003 (it was a bogus 0.9976 before in emulators).

Consequence: any elapsed-time measurement using `timer_us_gettime64()`/`timer_ms_gettime64()`
(or anything built on `timer_gettimespec()`) has a 0.25% slope error and a +2.5 ms step each
second. This affects earlier results in this repo (see errata notes in the other docs) and any
mGBA code that paces on those timers.

## Not established
- Behaviour under real load (mGBA emulation, video, GD-ROM); this ran on an idle SH-4.
- Deliberate underrun detection (not exercised: the underrun counter has never fired
  because no refill was delayed).
- Stereo, two-voice phase lock, ARM7 refill service, public API, channel discovery beyond the
  fresh-AFX assumption.
- Clicks at refill boundaries beyond the user's by-ear report; no line-out capture.
- One 60 s hardware run with the fixed clock (plus earlier runs that failed only on the artifact).

## AICA-domain clock cross-check (added later)
The ring test now also prints `AICA_CLOCK_CHECK`: AICAflow's own ARM7 timer-A tick counter
(`AFX_AICA_TIMER_TICK_ADDR`, SPU RAM 0x1fffe0, read by `afx_status_timer_ticks()`) against the
cursor's frame count, independent of any SH-4 timer. Real console, 20 s Mode A:

```
AICA_CLOCK_CHECK ticks=20048 cursor_frames=882271 frames_per_tick=44.0079 implied_tick_hz=1002.092
```
- The tick counter is **not** 1000 Hz. Timer A is loaded with `AFX_TIMER_RELOAD = 212`, so it overflows
  every 256 - 212 = 44 AICA samples = 1002.27 Hz; measured 1002.09 Hz (the 0.02% shortfall is
  about 3.6 ticks in 20 s, consistent with edge granularity and FIQ reload latency).
- Flow headers author at `tick_rate_num/den = 1000/1`, and I found no conversion to 1002.27 Hz in the
  places I grepped (`afx_compile_c.c`, `codec.c`, `driver.c`). If the ARM7 compares authored deadlines to
  this counter unscaled, authored timing/tempo runs about 0.2% fast (about 3.6 cents). **Unverified**:
  needs a long-WAIT flow measured against the cursor.
- Useful as an audio-domain second clock: cursor frames = 44.0 x ticks, with ~1 ms resolution. Too coarse
  for the ~50 us monitor settle, and it needs the firmware running (it is not a free-standing AICA counter).
