# AICA cursor probe

## Goal

Can SH-4 code read the AICA playback cursor (current sample index) of a looping
AICAflow voice **while the normal AICAflow ARM7 firmware stays active**, using the
AICA monitor registers? This is the gate for a possible SH-4-managed PCM ring.
Diagnostic only: no refill, no streaming API, no ARM7 changes, no `snd_stream`.

## KOS register mechanism (source)

`/opt/toolchains/dc/kos/kernel/arch/dreamcast/sound/arm/aica.c`, `aica_get_pos()`:

```c
SNDREG8(0x280d) = ch;                       /* observe channel ch */
for (i = 0; i < 20; i++) nop;               /* wait a while */
chans[ch].pos = SNDREG32(0x2814) & 0xffff;  /* sample index */
```

`SNDREG*` is `dc_snd_base + offset` (`sound/arm/aica.h`), i.e. ARM address
`0x00800000 + offset`. KOS only does this from the ARM7; it has no SH-4 version.

## Registers / constants used (SH-4 view)

| Item | Value |
|---|---|
| AICA register base (SH-4) | `0xa0700000` (ARM `0x00800000`; `AFX_AICA_REG_BASE`, and `dsp_scene.c` uses `0xa0702000`) |
| Monitor select | word at `+0x280c`, channel in bits 8..13 (byte `+0x280d`) |
| Monitor position | `+0x2814`, low 16 bits = sample index |
| SPU RAM (SH-4) | `0xa0800000` (`AFX_SPU_RAM_BASE_SH4`) |
| Access primitives | `g2_lock()` / `g2_unlock()`, `g2_read_32()`, `g2_write_32()`, `g2_write_8()`, `g2_write_16()` |

## AICAflow channel ownership and monitor sharing

- Channels are allocated by `allocate_channels()` in `driver/sh4/src/instance.c`
  and recorded in the channel-map arena in SPU RAM
  (`AFX_CHANNEL_MAP_ARENA_ADDR`; each entry is a 32-bit physical channel number).
- **Monitor sharing: AICAflow's ARM7 does not use the monitor.** `grep` of
  `driver/arm7/` finds no `0x280c`, `0x280d`, `0x2810` or `0x2814` access. Its
  `0x28xx` writes are `0x2804` (DSP ring), `0x289c`/`0x28b4`/`0x28a4` (interrupt
  setup at boot). SH-4 `driver/sh4/` also never touches the monitor. So the select
  is effectively owned by the SH-4 probe. This is a static source finding, not proof
  against a firmware change that adds monitor use later.
- AFX exposes no sample cursor: `position` fields in `protocol.h` are flow-stream
  positions.

## Physical-channel discovery

With a fresh `afx_init()`, the first instance occupies arena 0 entry 0 and the
second entry 1. The probe reads `map[0..3]` from SPU RAM with `g2_read_32`. Observed:
`map[0..3] = 0, 1, -1, -1` → reference voice on channel 0, probe voice on channel 1.
This relies on a single-voice-at-a-time, fresh-AFX assumption; a general solution
needs an API (the map address is private to `instance.c`).

## Implementation

`examples/aica_cursor_probe/`. Two looping voices from one 4096-frame sine bank
(16 cycles, 92.9 ms loop): a muted reference at pitch `0x0800` (88.2 samples/ms) on
channel 0, and the probe voice at pitch `0x0000` (44.1 samples/ms) on channel 1.
Steps: settle trace, selector scan, then a 10 s poll (10 ms interval) of the probe
channel and the reference channel, with a MIX `afx_instance_patch()` every ~100 ms as
concurrent AICAflow traffic. The scan picks the first select method / delay that
reads ch0 ≈ 88.2, ch1 ≈ 44.1 and idle channels ≈ 0 in all trials.

## Build

```
cd examples/aica_cursor_probe && source /opt/toolchains/dc/kos/environ.sh && make
```

Builds clean with `-Wall -Wextra -Werror`. Not added to the top-level `EXAMPLES`
list (diagnostic).

## Hardware command

```
cd examples/aica_cursor_probe
MGBA_DC_LOCK_TASK=aicaflow-cursor-probe \
  /home/gpf/code/dreamcast/mGBADC/tools/dc-load/kos-tool-locked.sh \
  -t 192.168.0.128 -x bin/aicaflow_aica_cursor_probe.elf
```

(Per `mGBADC/docs/TESTING.md`: always the locked wrapper; no ROM or `-m` needed.)

## Expected output markers

`AICA_CURSOR_PROBE_BEGIN`, `SETUP`, `TRACE`, `SCAN`, `SCAN_BEST`, `CURSOR`, `CURSOR_WRAP`,
`SUMMARY`, `CHECK <name>: PASS|FAIL`, and, only if every check passes,
`CURSOR PROBE READY FOR PCM REFILL EXPERIMENT`, then `AICA_CURSOR_PROBE_END`.

## Software sanity checks

advancing (>90% of polls move, no long constant runs), range (pos < 4096), wrap (≥10
in 10 s), rate within 3% of 44.1 samples/ms, select reaches the 2× reference
(88.2 ±3%), select distinguishes channels (reads differ from probe voice), playback
stable (instance stays PARKED, no failed patches).

## Hardware results (real Dreamcast, 2026-10-03)

Logs: `cursor-run1..6` in the session scratchpad. Six runs total; runs 5 and 6 are
the final design.

**Select method (the key finding).**

| Method | Result |
|---|---|
| 8-bit write to `0x280d` (what KOS's ARM driver does) | **Unreliable from the SH-4.** Runs 1–2 read the same channel regardless of select; the scan gave impossible rates (hundreds of samples/ms), i.e. reads from mixed channels. |
| 16-bit read-modify-write of `0x280c` | **Unreliable**; same symptoms. |
| **32-bit read-modify-write of `0x280c`** | **Works.** Scan (50/100/500 µs delay, 3 trials each, run 5 and run 6): ch0 = 88.1–88.7, ch1 = 44.1–44.4, idle ch 2/3/33 = 0.0, in all 9 trials per channel in each run. |

**Settle behaviour (w32 trace, selecting idle ch3 → playing ch0).** Reads at ~7 µs and
~15 µs still return the old (idle, 0) value; the new channel appears by ~21–23 µs. Once
selected, the value steps every ~22.7 µs (e.g. 1986, 1988, 1990, 1992 at 23, 45, 68,
90 µs), matching the 44.1 kHz sample clock. So the monitor refreshes once per AICA
sample period and needs **≥ ~25 µs** after a select; the 2 µs delay in the KOS-style
sequence is why the first attempts returned junk. Default used: 50 µs.

**Main 10 s run (w32, 50 µs), runs 5 and 6:**

| | run 5 | run 6 |
|---|---|---|
| polls | 873 | 845 |
| advancing | 873 | 845 |
| wraps | 107 | 107 |
| out of range | 0 | 0 |
| max pos | 4089 | 4090 |
| probe rate (expect 44.100) | 44.113 | 44.113 samples/ms |
| ref-channel rate (expect 88.200) | 88.227 | 88.227 samples/ms |
| ref same as probe | 0 | 0 |
| bad state / failed patches | 0 / 0 | 0 / 0 |

All 7 checks passed in both runs and the program printed
`CURSOR PROBE READY FOR PCM REFILL EXPERIMENT`. `LOOP_END = 4095` gives positions
0..4095 (max seen 4090 at 10 ms poll granularity), wrapping to 0.

## Conclusion

**Yes, with caveats.** From the SH-4, with the stock AICAflow ARM7 firmware running,
the cursor of a specific looping AFX voice can be read reliably, at about one-sample
resolution, provided that:

1. the channel is selected with a **32-bit read-modify-write** of the word at
   `0xa070280c` (not the byte write KOS's ARM driver uses),
2. the code waits **≥ ~25 µs** (50 µs used) between select and read, and
3. the select+delay+read sequence runs under `g2_lock()` and **only SH-4 code uses
   the monitor** (AICAflow's ARM7 does not today; this was verified by source search
   only).

Playback and the AFX instance stayed stable under 10 s of concurrent MIX patches, and
the cost is one ~50 µs spin per cursor read.

**Not established (do not infer):**
- No PCM ring, refill, underrun handling, or stereo behaviour has been tested.
- Poll granularity was 10 ms; this says nothing about sample-accurate cursor timing or
  how much latency a refill loop can tolerate. The cursor can be ~1–2 samples stale.
- The low byte of `0x280c` is written back with the value read (observed `0`); I did
  not verify that this cannot trigger anything (e.g. MIDI-out). Behaviour with a
  nonzero value there is untested.
- Channel discovery depends on a fresh AFX and arena-0 ordering; a real design needs a
  supported way to learn a voice's physical channel.
- Only one voice's monitor is read at a time; reading several channels per refill adds
  ~50 µs each.
- The scan runs once at startup; long-term stability beyond 10 s is untested.

CURSOR PROBE READY FOR PCM REFILL EXPERIMENT
