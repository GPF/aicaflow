# aica_stereo_cost_test

How much SH-4 time does a production-like stereo AICA ring cost? Same logic as `aica_stereo_ring_test`
(per-start integer offset compensation, left-cursor scheduling, 2048-frame halves, stock firmware), but
with a lean hot path: a precomputed period-2048 pattern (no per-sample generation), no read-back or L,R,L
triplet in steady state, integer tick timing. Busy time is split into cursor polling / uploads / afx_update+status /
everything else. 60 s at a 5 ms poll, then 20 s each at 10 and 20 ms polls. Results: `docs/results/aica-stereo-cost.md`.
