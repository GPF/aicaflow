# Assets and sidecars

## One playback model

Every sampled flow is one **AFB** sample bank plus one **AFX** control flow.
Several AFX files may bind to the same AFB, but an individual AFX never binds
to more than one bank. If a piece needs material from several SoundFonts or
sample sets, the offline tool combines the selected samples into that one AFB.

At runtime the SH-4 loads the AFB payload as one contiguous AICA allocation,
then validates and uploads AFX images that carry the bank's identity. The AFB
has no runtime sample name table or lookup index: each AFX setup already holds
the required bank-relative address and AICA register values.

| Extension | Role | Read by |
| --- | --- | --- |
| `.afb` | Sample bank; one contiguous encoded payload | SH-4 loader |
| `.afx` | Timed control stream and AICA setup templates | SH-4 loader, ARM7 executor |
| `.afc` | Optional seek checkpoints for one exact AFX/AFB pair | SH-4 only |
| `.afv` | Optional player visualisation frames | Player only |
| `.afp` | Offline performance/DSP profile | Offline authoring tool |
| `.afbm` | Editable source map for building a shared AFB | Offline bank builder |
| `.afi` | Reserved optional offline sample-offset index | No release runtime consumer |

All binary layouts are little-endian. Fixed headers make the files cheap to
validate and intentionally leave no alternate container or compatibility
encoding.

## AFB — sample bank

An AFB begins with exactly 32 bytes:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | magic `AFB\0` |
| 4 | 4 | version (`1`) |
| 8 | 8 | bank identity: low then high 32-bit word |
| 16 | 4 | payload offset (`32`, 32-byte aligned) |
| 20 | 4 | payload bytes |
| 24 | 4 | total file bytes |
| 28 | 4 | reserved, zero |

The payload is the exact encoded sample block copied into one AICA asset.
Individual samples may use PCM16, PCM8 or AICA ADPCM in the same bank; their
format, loop points and offsets are already represented in the AFX setup
records. The runtime recomputes neither a hash nor a checksum over sample data;
it compares the precomputed identity in the AFB and AFX headers.

## AFX — control flow

An AFX begins with a fixed 80-byte header (`AFX2`, file version `7`). Its
important fields are total size, flags, image offset and size, stream offset
and size inside that image, control identity, setup count, bound AFB identity,
relocation table location/count, required channel count, tick-rate numerator and
denominator, and the work profile. The image offset is 32-byte aligned.

Each relocation gives a setup-pair offset, a bank-relative sample offset and
its byte length. The loader checks it against the AFB payload and resolves the
address once. The timed stream only contains existing ARM7 bytecode operations;
AFX has no embedded samples, no sample names and no runtime parser extension.

`control_id` identifies the exact control image. It changes when an offline
profile derives a new AFX and lets its AFC sidecar be rejected if stale.

## AFC — seek sidecar

An AFC begins with exactly 32 bytes:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | magic `AFC\0` |
| 4 | 4 | version (`1`) |
| 8 | 4 | AFX control identity |
| 12 | 8 | AFB identity: low then high word |
| 20 | 4 | payload offset (`32`) |
| 24 | 4 | payload bytes |
| 28 | 4 | total file bytes |

The payload is a checkpoint table used only by the SH-4 when a player offers
seek. Normal playback neither loads nor needs AFC. It is deliberately separate
so applications without seeking do not carry its SH-4 memory cost.

## AFV — visualisation sidecar

AFV is a compact player asset, not a DSP analysis format. Its `VIZ1` header is
12 bytes: version, band count, frame rate and frame count. The release compiler
writes 32 one-byte bands per frame at 60 Hz. A player may omit AFV with no
effect on audio playback or seeking.

## AFP — performance profile

AFP is editable JSON and is bound to one base AFX by its AFX file version and a
SHA-256 digest. It is an offline transform: it writes a derived AFX and matching
AFC, never a profile interpreter on Dreamcast. Timing belongs to the source
MIDI/control flow; AFP is for timbre, articulation and DSP treatment.

The release schema starts like this:

```json
{
  "format": "aicaflow.afp",
  "version": 1,
  "base": { "abi": 7, "canonical_sha256": "..." },
  "dsp": { "preset": "room", "send": 112 },
  "templates": {
    "all-notes": { "label": "All notes", "parameters": { "dsp_send": 112 } }
  },
  "assignments": [
    { "event": { "kind": "note", "tick": 0, "ordinal": 0, "channel": 0 }, "template": "all-notes" }
  ]
}
```

`init` writes a complete note inventory so an editor has stable event identity.
An ordinal is the zero-based position among note events at the same tick; it is
not a timing offset. Today `afx_profile_c apply` applies the common DSP preset
and send to the setup templates and rewrites AFX/AFC identities. It validates
the whole profile binding but does not yet use individual assignments to make
per-note setup copies. That extension remains an offline compiler task and
must continue to emit ordinary AFX commands.

Supported preset names are `dry`, `room`, `room_warm` and `room_large`;
`dry` requires send `0`, while a room preset requires a nonzero send.

## AFBM — bank map

AFBM is a human-edited text input to `afx_bank_c`, not a runtime asset. It
combines several MIDI files and SoundFonts into one shared bank while keeping
one bank binding per generated flow:

```text
source gm        soundfonts/GeneralUser.sf2
source orchestra soundfonts/orchestra.sf2

# map <song|*> <midi-bank> <midi-program> <source> <sf2-bank> <sf2-program> <format>
map * 0 0  gm        0 0  auto
map * 0 42 orchestra 0 42 pcm16

song title_theme midi/title_theme.mid
song field_theme midi/field_theme.mid
```

`source` names an SF2 input. `map` routes a MIDI bank/program to an SF2 preset
and chooses `pcm16`, `pcm8`, `adpcm` or `auto`; a song-specific map overrides
`*`. `song` selects the MIDI sources. `auto` uses the deterministic offline
quality gate and conservatively avoids ADPCM for looped sources. The bank
builder writes the shared `.afb` plus one `.afx`, `.afc` and `.afv` for each
song. `--create-map` creates an editable starting AFBM from an SF2 and MIDI
files.

## AFI — reserved index

AFI is reserved for an optional authoring/editor index: a bound AFB identity,
sample count, and sample payload offsets, with optional names where source
metadata provides them. It must never be required for playback, placed on
ARM7, or used as another runtime bank format. No release tool emits or consumes
AFI yet.
