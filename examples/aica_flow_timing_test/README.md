# aica_flow_timing_test

Does an authored AFX `WAIT` of N ticks (flow header `tick_rate 1000/1`) last N ms of real AICA
playback, or N / 1002.27 s (the ARM7 timer-A counter measured ~1002.1 Hz in
`docs/results/aica-ring-mode-a.md`)? One looping voice; flow = `NOTE_PL`, `WAIT32 N`, `PATCH`
(pitch x2), `WAIT16 500`, `KEYOFF`, `END`. The SH-4 polls the AICA cursor (frames since key-on)
and detects the pitch change as the point where per-poll advance doubles. The verdict uses
cursor frames only; `timer_us_gettime64()` is not used. Results: `docs/results/aica-flow-timing.md`.
