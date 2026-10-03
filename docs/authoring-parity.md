# C authoring parity

The release authoring path is C.  The retired Python programs in
`tools/research/` are a behavioural oracle only: they describe accepted
conversion choices, but are neither invoked by `make` nor required by a user.

The goal is not byte identity with an arbitrary historic file.  Sample order,
alignment and valid AFX dictionary packing may differ without changing the
rendered AICA program.  Parity means that, from the same MIDI, SoundFont and
declared sidecars, the C tools emit valid bank-bound AFB/AFX/AFC/AFV files with
the same:

- selected source zones, channels, decoded rate, coding, loop bounds and
  release-tail policy;
- initial AICA sample, envelope, pan, filter, mix, LFO and DSP-send words;
- NOTE, KEYOFF and offline-generated PATCH timing; and
- whole-flow DSP scene and tempo choice.

`tools/test` must contain one compact C check per newly lowered policy.  The
music-player build is the integration check: it builds the three published
works without Python and validates every generated asset.

## Ownership

| Input | Owns |
| --- | --- |
| MIDI | notes, tempo, pedals and all musical timing |
| AFBM | source selection and all MIDI/SF2-to-AICA conversion policy |
| AFP | offline AICA performance transform: DSP scene, register templates,
  note overrides and generated PATCH lanes |
| AFB/AFX/AFC/AFV | generated runtime assets only |

An AFP never changes an AFB.  In particular, sample format, resampling,
looping and SF2 lowering are AFBM settings, so a bank is reproducible from its
map.

## AFBM map syntax

The stable map prefix is:

```text
map <song|*> <midi-bank> <midi-program> <source> <sf2-bank> <sf2-program> <format> [key=value ...]
```

The named tail avoids a growing, ambiguous column list.  Each option is
per-map; a song may therefore combine banks and policies while still producing
one AFB.

`midi_channel=<0..15>` narrows a mapping to one source MIDI channel. It is
resolved before the generic map for the same song/program, allowing a score to
reuse a program number for musically different parts without a runtime branch.

Initially supported options are documented in `docs/authoring.md`.  The
complete parity set is:

1. sample coding/rate/channel, loop trimming and natural release-tail;
2. amplitude-envelope, gain, pan, static/filter-envelope and source-send
   lowering;
3. static LFO and an explicit NOTE-on policy for CC1, CC7, CC10, CC11, CC91 and CC93;
4. deterministic offline timbre variation; and
5. AFP note lanes, which lower deliberate sustained changes into ordinary
   existing AFX PATCH commands.

Items 3--5 create no ARM7 feature and no runtime profile interpreter.  The C
authoring tools validate the existing per-tick AFX command/write budgets when
they emit their patches.

## Delivery order

- [x] Replace the brittle optional map columns with named AFBM conversion
  settings.
- [x] Carry map settings through the C SF2 resolver, including explicit source
  pan/reverb policy, fixed or SF2 amplitude envelope, direct path, static LFO,
  loop trimming and safe one-shot tails.
- [x] Snapshot MIDI controllers and lower supported linear SF2 graph values at
  NOTE-on under the explicit `modulators=apply|ignore` AFBM policy.
- [ ] Lower controller automation after NOTE-on (CC1, CC7, CC10, CC11, CC91
  and CC93) into AFX PATCH commands under an explicit AFBM policy.
- [x] Add note-selected AFP lanes for deliberate sustained gain/LFO/filter
  changes. Static defaults and templates remain the compact all-note and
  family-level form; a lane is deliberately specific to one sounding voice.
- [x] Export the accepted Bach/Chopin/Grieg performance choices to declarative
  sources: Bach timing is checked-in MIDI, while Chopin and Grieg sustained
  expression is checked-in AFP lanes.
- [ ] Add the semantic comparison harness and capture-test the resulting
  corpus on hardware.

## Migration and proof

The three classical works are the reference corpus.  For each work we retain
the source MIDI, pinned SoundFont and human-authored AFBM/AFP source.  A test
compares the generated assets' semantic records with a checked historical
reference: setups and sample metadata, event stream, control identity,
sidecar binding and visual timeline.  It intentionally does not compare AFB
byte offsets or setup dictionary ordering.

Hand-authored historic performance data is exported into AFP templates,
overrides and lanes.  It is not silently reconstructed from a binary AFX, and
it is not kept as hidden Python behaviour.

The published classical profiles are deliberately sparse: Chopin carries 1,146
valid sustained-expression lanes, Bach carries only its shared `room_large`
scene/send, and Grieg contains only its shared room scene/send. The two omitted legacy Chopin
commands have an empty PATCH mask, so they change no AICA register and are
intentionally not emitted. The legacy pre-release files themselves are not a
second supported AFX format: this was a one-time source migration into AFP v4.
