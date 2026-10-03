# aica_cursor_probe

Diagnostic only. Answers one question: can the SH-4 read the AICA playback
cursor of an AICAflow-owned looping voice while the normal AICAflow ARM7
firmware runs? No PCM ring, no refill, no `snd_stream`, no ARM7 changes.

Starts a muted 2x-rate reference voice (physical channel 0) and a 44.1 kHz
sine-loop probe voice (channel 1), scans SH-4 monitor-select methods, then
polls the probe cursor for 10 s while patching MIX every 100 ms. Prints
`CHECK ...: PASS/FAIL` lines and, only if all pass,
`CURSOR PROBE READY FOR PCM REFILL EXPERIMENT`.

Results and analysis: `docs/results/aica-cursor-probe.md`.
