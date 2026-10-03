# aica_ring_test

Mono PCM16 ring experiment for a possible SH-4-managed AICA PCM stream.

**Phase 1 (this build): upload bandwidth sweep only.** One looping AICAflow voice
plays a 4096-frame sine ring (+4 guard frames) while the SH-4 repeatedly rewrites the
start of the ring (with the *same* data, so audio stays a clean tone) using
`afx_mem_upload()`. Reports per-call timing, back-to-back burst throughput, linearity,
read-back verification, and whether the playback cursor keeps pace with wall time.

Not implemented yet: the refill loop, underrun detection, stereo. Bandwidth alone does
not establish streaming viability. Results: `docs/results/aica-ring-bandwidth.md`.
