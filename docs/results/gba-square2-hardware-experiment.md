# GBA Square 2 — AICAflow hardware experiment

## Purpose

Validate that AICAflow can reproduce GBA Square 2 channel behaviour on one
AICA hardware voice: waveform duty, pitch accuracy, static level, and live
`afx_instance_patch()` delivery. **Not** an mGBA integration.

## Implementation

- **Repository**: `examples/gba_square2_test/` (standalone, no external assets)
- **Waveform**: 8-sample PCM16 looping buffer, duty patterns from
  `mGBADC/src/gb/audio.c` `_squareChannelDuty[4][8]`
- **PCM values**: `+32767 / -32767` (no `+32768` in signed 16-bit)
- **Frequency formula** (verified against mGBA source):
  `freq = 524288 / (2048 - frequency_register) Hz`
  - reg 0 → 256 Hz, reg 857 → 440.2 Hz, reg 1536 → 1024 Hz
- **AICAflow pitch word**: bits 11-14 = signed octave, bits 0-9 = 10-bit FNS
  `ratio = 2^octave × (1 + FNS/1024)`
  For an 8-sample loop at 44.1 kHz, unpitched cycle rate = 5512.5 Hz.
  Pitch computed against that rate, **not** the 261.6256 Hz C4 reference.
- **Level**: MIX high byte is TL attenuation (0 = loud, 0xff = mute); GBA volume v maps to att = (15-v)*2, v=0 → mute. Approximate, not a calibrated GBA curve.
- **Runtime ramp**: `afx_instance_patch()` on `AFX_FIELD_MIX` one step per 1/64 s (15.625 ms, GBA envelope tick), absolute deadlines; each patch logs due/sent/late µs
  (1/64 s per step), 16 steps: 15→0
- **No** `snd_stream` calls, **no** DSP, **no** PCM streaming, **no** ARM7
  firmware changes

## Waveform construction

Duty patterns (8 steps each, 0 = silence, 1 = tone):

| Pattern | mGBA source | Output |
|---------|-------------|--------|
| 0 | `{0,0,0,0,0,0,0,1}` | 1/8 duty |
| 1 | `{1,0,0,0,0,0,0,1}` | 1/4 duty |
| 2 | `{1,0,0,0,0,1,1,1}` | 1/2 duty |
| 3 | `{0,1,1,1,1,1,1,0}` | 3/4 duty |

Transformed to signed PCM16: `0 → -32767`, `1 → +32767`.

## AICAflow APIs used

| API | Purpose |
|-----|---------|
| `afx_init()` | Firmware load |
| `afx_instance_activate()` | Start voice |
| `afx_instance_patch()` | Live pitch/level (Phase 4) |
| `afx_instance_stop()` | Stop voice |
| `afx_instance_status()` | Status polling |
| `afx_bank_flow_upload()` | Build AFX from memory |
| `afx_asset_free()` | Free flow |
| `afx_shutdown()` | Teardown |

## Build

```sh
source /opt/toolchains/dc/kos/environ.sh
make -C examples/gba_square2_test
make -C examples/gba_square2_test bin/aicaflow_gba_square2_test.cdi
```

ELF: `examples/gba_square2_test/bin/aicaflow_gba_square2_test.elf` (2.1 MB)

## Hardware run instructions

```sh
kos-tool -t 192.168.0.128 -x examples/gba_square2_test/bin/aicaflow_gba_square2_test.elf
```

## Expected serial markers

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

## Capture procedure

1. Run on real Dreamcast hardware (dc-load / kos-tool)
2. Capture AICA output via audio interface (line-out / S/PDIF)
3. Compare captured waveform against expected duty patterns
4. Measure frequency accuracy with FFT or zero-crossing
5. Verify level ramp shows 16 distinct steps

## Pass/fail thresholds

| Criterion | Threshold |
|-----------|-----------|
| Pitch error | ≤ 0.1 % |
| Duty transition | Within 1 captured 44.1 kHz sample |
| Steady waveform RMS error | ≤ 5 % full scale |
| Level 0 | Silent |
| Level 7 > Level 0 | Yes |
| Level 15 > Level 7 | Yes |
| Ramp: every PATCH accepted | No queue overflow |
| Ramp: 16 steps visible | Yes |

## Current result status

- **Software**: PASS (builds clean, no warnings)
- **Hardware**: runs to completion on the console (all phases, 16/16 patches OK, SH-4 submit lateness ≤3 µs). Audio capture, pitch/duty/level measurement and ARM7 apply-time NOT YET MEASURED
- **ARM7 firmware**: NOT CHANGED
- **Runtime API changes**: NONE (used existing `afx_instance_patch()`)

## Files created

| File | Purpose |
|------|---------|
| `examples/gba_square2_test/Makefile` | enDjinn build entry |
| `examples/gba_square2_test/local.cfg.mk` | Build config |
| `examples/gba_square2_test/code/main.c` | Test program |
| `examples/gba_square2_test/README.md` | Run instructions |
| `docs/results/gba-square2-hardware-experiment.md` | This document |
| `Makefile` (M) | Added `gba_square2_test` to EXAMPLES |

## Git status

```
M Makefile
?? examples/gba_square2_test/
```

No whitespace errors (`git diff --check` clean).

## Ready for hardware?

Yes — ELF builds, no ARM7 changes, no API changes, serial markers are clear.
Run on Dreamcast and capture audio for analysis.
