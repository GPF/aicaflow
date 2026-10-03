# GBA Square 2 — AICAflow hardware experiment

Standalone test of GBA Square 2 channel behaviour on one AICA hardware voice.
**Not** an mGBA integration; validates waveform, pitch, level, and live patch
delivery on real Dreamcast hardware.

## What it tests

| Phase | Question |
|-------|----------|
| 1 — Pitch | Can AICAflow pitch represent 256 / 440.2 / 1024 Hz accurately? |
| 2 — Duty | Can one 8-sample looping waveform reproduce all four GBA duty patterns? |
| 3 — Static level | Do TL 0/7/15 produce silence / increasing amplitude? |
| 4 — Runtime ramp | Can `afx_instance_patch()` deliver 16 level steps (1/64 s apart, GBA envelope tick) reliably? |

## Build

```sh
source /opt/toolchains/dc/kos/environ.sh
make -C examples/gba_square2_test
make -C examples/gba_square2_test bin/aicaflow_gba_square2_test.cdi
```

## Run on hardware

```sh
# Load via dc-load / kos-tool
kos-tool -t 192.168.0.128 -x examples/gba_square2_test/bin/aicaflow_gba_square2_test.elf
```

## Expected serial output

```
GBA_SQ2_TEST_BEGIN
PHASE=PITCH
PITCH freq_reg=0 target_hz=256 aica_pitch=0x59F2
PITCH freq_reg=857 target_hz=440 aica_pitch=0x611C
PITCH freq_reg=1536 target_hz=1024 aica_pitch=0x69F2
PHASE=DUTY
DUTY index=0 pattern={0,0,0,0,0,0,0,1}
DUTY index=1 pattern={1,0,0,0,0,0,0,1}
DUTY index=2 pattern={1,0,0,0,0,1,1,1}
DUTY index=3 pattern={0,1,1,1,1,1,1,0}
PHASE=LEVEL
LEVEL requested=0 aica_tl=0
LEVEL requested=7 aica_tl=7
LEVEL requested=15 aica_tl=15
PHASE=RAMP
PATCH level=15 submit=OK
PATCH level=14 submit=OK
...
PATCH level=0 submit=OK
GBA_SQ2_TEST_READY_FOR_CAPTURE
Aicaflow gba_square2_test: PASS (0)
```

## Pass/fail (hardware)

- **Pitch**: measured frequency error ≤ 0.1 %
- **Duty**: each duty transition within one 44.1 kHz sample after alignment
- **Static level**: TL 0 = silent; TL 7 > TL 0; TL 15 > TL 7
- **Ramp**: every PATCH accepted; no queue overflow; waveform shows 16 steps
- **Steady waveform**: RMS error ≤ 5 % full scale after gain normalisation

All measurement results are recorded in `docs/results/gba-square2-hardware-experiment.md`.
