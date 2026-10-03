# aica_stereo_ring_test

Stereo SH-4-managed ring with host-side offset compensation. Two looping voices (L hard left, R hard right)
start 18-19 samples apart (`docs/results/aica-stereo-lock.md`). Per start the SH-4 measures the R-L offset
(L,R,L cursor bursts), rounds it to whole samples, and stores the right ring shifted by it. Refills for both
rings are scheduled from the LEFT cursor only (right = left + offset); a per-poll L,R,L triplet checks that
premise. Signal: loud click train (identical in both channels) + quiet 440 Hz (L) / 646 Hz (R) tones.
Compensation alternates ON/OFF every 5 s to listen for image shift. 3 sessions, each recalibrated.
Results: `docs/results/aica-stereo-ring.md`.
