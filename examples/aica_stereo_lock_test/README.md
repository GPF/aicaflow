# aica_stereo_lock_test

Do two AICAflow voices started by one flow stay sample-phase locked? (Prerequisite for driving a
stereo ring from a single cursor.) One 2-channel flow: left (hard left, 440 Hz ring) and right
(hard right, 646 Hz ring) both `NOTE_PL` at tick 0. The SH-4 reads L, R, L and interpolates L to the
instant of the R read for a sub-sample R - L offset. Phases: 6 separate starts (start skew), 20 s
steady, 20 s with continuous ring uploads. Results: `docs/results/aica-stereo-lock.md`.
