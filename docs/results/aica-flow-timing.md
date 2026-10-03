# Authored AFX flow timing: is a "tick" 1 ms?

## Question
Flow headers author at `tick_rate_num/den = 1000/1`. The ARM7 tick counter is timer A with
`AFX_TIMER_RELOAD = 212` (256 - 212 = 44 AICA samples = 1002.27 Hz; measured ~1002.1 Hz in
`aica-ring-mode-a.md`). Does an authored `WAIT` of N ticks last N ms of AICA playback, or N / 1002 s?

## Source facts (read, not just measured)
- ARM7: `schedule_wait()` (`driver/arm7/driver.c:116`) does `deadline += (step * tempo_period_q8_8 + frac) >> 8`
  (tempo 256 = normal), and `service()` fires when `*CLOCK >= deadline` (`tick_due`, line 110). `*CLOCK`
  is the timer-A FIQ counter. No 1000 -> 1002.27 conversion exists on that path.
- Authoring (`afx_compile_c.c:445`) converts musical time to ticks with the header's `tick_rate_num/den`
  (1000/1 in the files I looked at).

## Method (`examples/aica_flow_timing_test`)
One looping voice. Flow = `NOTE_PL` (native pitch), `WAIT32 N`, `PATCH` pitch x2, `WAIT16 500`, `KEYOFF`,
`END`, header tick rate 1000/1. The SH-4 polls the AICA cursor every 1.5 ms (w32 select, 50 us settle);
the cursor counts frames since key-on, so the poll where the per-poll advance exceeds 1.25x the
baseline marks the pitch change. Frames at that point = AICA audio time for N authored ticks.
The verdict uses cursor frames only. The SH-4 timer (corrected clock) only paces the polls.

## Hardware result (real Dreamcast, 2026-10-03, two runs)

| authored ticks | measured frames (run 2) | run 1 | 1000 Hz predicts | 44-sample ticks predict | implied tick rate |
|---|---|---|---|---|---|
| 10 000 | 440 088 (+-54) | 440 090 | 441 000 | 440 000 | 1002.07 Hz |
| 20 000 | 880 258 (+-58) | 880 275 | 882 000 | 880 000 | 1001.98 Hz |

- Error versus 1000 Hz: -0.207% and -0.198%. Error versus 44-sample ticks: +0.020% and +0.029%.
- 10 000 authored ticks last 9.979 s of AICA time (not 10.000 s); 20 000 last 19.96 s.
- The AICA tick counter advanced exactly as authored (10 000 and 20 001 ticks between the two events),
  so deadlines are consistent with the counter; it is the counter that is not 1 ms.
- Frames per tick is 44.009 to 44.013, i.e. 44 samples plus roughly 0.01 sample (about 0.2 us) of
  FIQ reload latency per tick (my inference; not separately measured).

## Conclusion
**Authored flow timing is about 0.2% fast.** An AFX tick is ~44.01 AICA samples (~1002.0 Hz), not 1 ms.
A 3-minute authored piece finishes about 0.4 s early; tempo is about 3.6 cents sharp. Anything that treats
a tick as a millisecond (authoring at 1000/1, `afx_status_timer_ticks()` as ms, SH-4 pacing against it)
inherits this.

## Not established / next
- Whether any shipped example or asset relies on 1 tick = 1 ms. I have not audited them.
- Fix options (no change made): (a) author with the true rate (header `tick_rate_num/den` ~ 44100/44.01,
  or exactly 11025/11 if the FIQ latency is ignored) so the compiler places events in true time;
  (b) have the ARM7 scale deadlines; the existing q8.8 tempo path is too coarse for 0.23% (256.58 -> 257);
  (c) change the timer so a tick is exactly 44.1 samples (not possible with an integer reload);
  (d) just document "tick = 44 samples". Choosing is a contract decision for AICAflow.
- Single hardware, one voice, flow at normal tempo.

## Fix: the authoring time base is now the Timer-A rational 11025/11
Option 1 was taken, using the exact hardware-derived rational (44 samples per tick, `44100/44 = 11025/11`)
rather than the measured integer 1002:
- `AFX_TICK_RATE_NUM/DEN = 11025/11` in `protocol.h`; helpers `afx_usec_to_ticks`, `afx_samples_to_ticks`,
  `afx_ticks_to_usec` in `codec.h`.
- C tools: `afx_compile` (CLI, MIDI, SF2), `afx_bank` (song manifest; an explicit integer rate still overrides),
  `afx_n64` (also its us -> tick conversions), `afx_vgm`, `afx_demo_assets` all author at `AFX_C_TICK_RATE`.
  The compiler/MIDI/cseq APIs now take an `afx_c_tick_rate_t {num, den}` instead of a bare integer.
- Examples that hand-build headers or convert time (`dynamic_sfx`, the AICA diagnostics, `music_player`'s
  ticks <-> ms) use the shared constants. Python research compilers keep the nominal 1000/1 but are labelled
  and print a warning. Docs: `docs/authoring.md` "Time base".
- Regression coverage: `test_tick_rate_contract()` (header carries 11025/11; 1 s MIDI note = 1002 ticks;
  10 s = 10023; 11 s = 11025; explicit 1000/1 still honoured), a header assertion in the `make check` CLI
  fixture, and one in `test_afx_vgm.py`.

### Hardware re-test (real Dreamcast, same test, authored in seconds)
| authored | ticks | measured frames | expected (true time) | error | verdict |
|---|---|---|---|---|---|
| 10 s | 10 023 | 441 174 (+-66) | 441 000 | +0.040% | PASS |
| 20 s | 20 045 | 882 260 (+-66) | 882 000 | +0.030% | PASS |

Before the fix the same test gave -0.2%. The remaining +0.03% to +0.04% is real: a tick is 44.014-44.016
samples, not 44, because the FIQ reloads timer A a little after the overflow (about 0.4 us per tick, my
inference). If tighter agreement is ever needed, the header rate could be refined to the measured value
without changing any code. Pass threshold in the test: within 0.05% of true audio time.

### Caveats
- `make check` still cannot run end to end (the unrelated `afx_bank --merge` / `--per-song` AFC mismatch, which
  also stops `examples/music_player` asset preparation at HEAD); the individual tests were run instead:
  `test_afx_compile`, `test_afx_n64_cseq`, `test_afx_n64_sfx`, `test_afx_vgm.py`, `driver` tests.
- `music_player` could not be built end to end for that reason; its two new tick/ms helpers were checked
  natively. Assets built before this change keep their 1000/1 header and old timing until rebuilt.
- The compiler's checkpoint interval (`afx_compile_c.c`, every 1000 ticks) is an interval in ticks and was left.
