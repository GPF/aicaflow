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
| `.afi` | Optional binary AFB sample catalog | SH4-side one-shot code |

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
writes 32 one-byte bands per frame at 60 Hz from the active notes' pitch,
authored level and a short visual decay, normalized across the piece's actual
pitch range. It is deliberately an inexpensive musical-energy animation, not a
PCM FFT or a measurement of the final DSP mix. A player may omit AFV with no
effect on audio playback or seeking.

## AFP — performance profile

AFP is editable JSON and is bound to one base AFX by its AFX file version and a
SHA-256 digest. It is an offline transform: it writes a derived AFX and matching
AFC, never a profile interpreter on Dreamcast. Note timing belongs to the source
MIDI/control flow; AFP is for timbre, articulation, DSP routing and an optional
whole-flow playback rate.

```json
{
  "format": "aicaflow.afp",
  "version": 3,
  "base": { "afx_version": 7, "canonical_sha256": "..." },
  "dsp": { "preset": "room_warm" },
  "tempo_q8_8": 220,
  "defaults": { "dsp_send": 128 },
  "templates": {
    "cello": { "parameters": { "lfo": 19024 } },
    "close": { "parameters": { "dsp_send": 0 } }
  },
  "setup_templates": { "0": "cello" },
  "overrides": [
    { "event": { "kind": "note", "tick": 750, "ordinal": 0, "channel": 0 },
      "template": "close" }
  ]
}
```

The effective parameters for a note are resolved in this order:

`base AFX setup → defaults → setup template → note template → note parameters`.

An ordinal is the zero-based position among note events at the same tick; it is
only an offline selector, never a timing offset. `build/afx_profile inventory
song.afx` prints the available selectors and their source setups. `init` writes
the compact empty form rather than hundreds of redundant all-note assignments.

`dsp.preset` installs **one scene for the whole flow**. AICA cannot run a
different DSP program for each note. `dsp_send`, however, is an ordinary AICA
channel register: a global default can send every note to that scene, a template
can change the send for a family of notes, and one override can make a selected
note drier or wetter. The same precedence works for `env_ad`, `env_dr`, `lfo`,
`direct`, `mix`, `filter_level0` through `filter_level4`, `filter_ad` and
`filter_dr`. These are raw 16-bit AICA register words so the profile lowers to
existing `NOTE` and `PATCH` commands; later source patches cannot overwrite a
profiled field while that note is active.

`tempo_q8_8` is optional and applies to the whole flow: `256` is authored
tempo, `128` is half speed and `512` is double speed. The SH4 sends that
existing instance-tempo value when it activates the flow, so it changes neither
the AFX command stream nor the individual NOTE/KEYOFF offsets. It is useful for
choosing an overall performance pace, not for rhythmic humanisation.

Supported preset names are `dry`, `room`, `room_warm` and `room_large`. A dry
preset with empty defaults is a useful byte-identical profile: applying it
copies both AFX and AFC unchanged.

## AFBM — bank map

AFBM is a human-edited text input to `afx_bank`, not a runtime asset. It
combines several MIDI files and SoundFonts into one shared bank while keeping
one bank binding per generated flow:

```text
source gm        soundfonts/GeneralUser.sf2
source orchestra soundfonts/orchestra.sf2 stereo

# map <song|*> <midi-bank> <midi-program> <source> <sf2-bank> <sf2-program> <format>
map * 0 0  gm        0 0  auto
map * 0 42 orchestra 0 42 pcm16

song title_theme midi/title_theme.mid
song field_theme midi/field_theme.mid
```

`source` names an SF2 input and may end with `stereo` (the default), `left`,
or `right`. The latter two select one linked side and centre it on AICA, which
is useful when a detailed stereo library will not fit in RAM. `map` routes a
MIDI bank/program to an SF2 preset and chooses `pcm16`, `pcm8`, `adpcm` or
`auto`; a song-specific map overrides `*`. `song` selects the MIDI sources.
`auto` uses the deterministic offline quality gate and conservatively avoids
ADPCM for looped sources. The bank builder writes the shared `.afb` plus one
`.afx`, `.afc` and `.afv` for each song. `--create-map` creates an editable
starting AFBM from an SF2 and MIDI files.

The native import path lowers static SF2 envelope, attenuation, pan,
reverb-send and filter controls into the existing AICA setup words and NOTE
levels. AFX never embeds SF2 data or a generic modulator interpreter.

## AFI — SH4 sample catalog

AFI is the optional binary catalog that lets SH4-side code start AFB samples
as direct one-shots without scanning an AFX setup dictionary. It is never
uploaded to AICA or interpreted by ARM7. The bank builder emits two variants
next to every AFB: `bank.afi` has compact records and `bank.names.afi` adds
fixed-width source sample names. Both bind to exactly the same AFB.

An AFI begins with a fixed, 32-byte little-endian header:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | magic `AFI\0` |
| 4 | 4 | version (`1`) |
| 8 | 8 | AFB identity: low then high word |
| 16 | 4 | record offset (`32`) |
| 20 | 4 | unique sample count |
| 24 | 4 | record bytes (`16` or `32`) |
| 28 | 4 | total file bytes, padded to 32 bytes |

Each compact 16-byte record is little-endian:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | sample **file offset** in the AFB |
| 4 | 4 | effective sample rate in Hz |
| 8 | 4 | decoded sample length in frames |
| 12 | 1 | AICA format (`0` PCM16, `1` PCM8, `2` ADPCM) |
| 13 | 3 | reserved, zero |

The 32-byte named record appends a zero-padded, fixed 16-byte source sample
name at offset 16. Duplicate AFX setups that refer to the same AFB sample
produce one AFI record. AFI currently describes direct one-shots: loop points,
root-key and tuning deliberately remain AFX setup data.
