# C authoring parity

The release asset-authoring path is C. Python programs in `tools/research/`
describe historic accepted choices and serve as reference/experimental tools.
Production example asset generation does not invoke them; `make check` does
run Python reference tests. DKR's application pack/playlist orchestration and
the tuner client also use Python, without moving synthesis onto the host.

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

## N64 CSeq authority

For N64 imports, the compact CSeq event stream and the game's original
`ALCSPlayer`/ALBank semantics are the authority, not the retired Python
importer.  `afx_n64` applies the DKR player rules directly: initial and
program-selected instrument pan/volume/bend range, sample pan, CC7 volume,
CC10 pan, CC91 FX mix and pitch bend.  Changes that affect a live voice become
ordinary AFX `PATCH`/`PATCH_LEVEL` commands.  The DKR corpus contains no other
live controls; source controls that its original player ignores remain ignored
here too.  Python is retained only as a regression comparison, so a historic
Python bug is never a reason to preserve incorrect source behaviour.

The native SFX lowering has a sanitizer-backed regression check in
`tools/test/test_afx_n64_sfx.c`, run by `make check`. It covers live-control
template pitch/level, negative-decay sustain, a finite component in a parked
chain, source-rate duration, the PCM quality fallback and truncated codebooks.
The bank merge check also requires unchanged sample payloads. SFX coding and
rate candidates follow the accepted Python SFX policy; `--merge` only
deduplicates and relocates, never re-encodes final samples.

The complete 784-sound DKR corpus was also compared with the accepted Python
SFX generator using the same B1/table input: all 1,371 component NOTEs match
in start time and playback state, all encoded sample bytes and loop bounds
match, and KEYOFF/END/PARK timing matches. Each template retains its NOTE's
pitch/level baseline for live SH4 controls. Bank/control identity hashes are
bookkeeping and need not match the retired Python writer.

## Ownership

| Input | Owns |
| --- | --- |
| MIDI | notes, tempo, pedals and all musical timing |
| N64/VGM source | source-specific notes, controls and timing; no MIDI round-trip |
| AFBM | source selection and all MIDI/SF2-to-AICA conversion policy |
| AFP | offline register templates, note overrides, PATCH lanes and build-time DSP/tempo choices |
| AFSFX | application SFX grouping/residency policy; not sample conversion |
| AFB/AFX/AFC/AFV/AFI | generated assets only |

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

## Implemented policy versus remaining limits

Named AFBM conversion settings, static SF2 lowering, supported linear modulator
snapshots and note-selected AFP PATCH lanes are implemented. Bach's rhythmic
performance is checked-in MIDI; the retained sustained Chopin expression is
AFP data. Global defaults and source-setup templates avoid repeating per-note
settings when they are shared.

General MIDI controller automation after NOTE-on is not yet lowered to PATCH
under an AFBM policy. This is distinct from native CSeq: its supported live
controls do produce source-derived PATCH commands. SF2 curved/time-varying
modulator graphs and a native OoT AudioSeq importer are not claimed here.

`make check` is not an exhaustive historical music/corpus comparison or a
hardware listening test. The external DKR SFX corpus comparison above was
performed on extracted inputs; those inputs are not distributed. A complete
checked-in semantic comparison harness for every historical classical output
is not currently provided. Do not infer blanket parity from a passing synthetic
test or from identical file sizes.

## Migration and proof

The three classical works are the reference corpus.  For each work we retain
the source MIDI, pinned SoundFont and human-authored AFBM/AFP source. Compare
semantic records when evaluating a rebuilt historical reference: setups and
sample metadata, event stream, binding and visual timeline. Packing offsets,
dictionary order and identity hashes can differ after legitimate relocation;
they do not on their own prove an audible difference.

Hand-authored historic performance data is exported into AFP templates,
overrides and lanes.  It is not silently reconstructed from a binary AFX, and
it is not kept as hidden Python behaviour.

The published classical profiles are deliberately sparse: Chopin carries 1,146
valid sustained-expression lanes, Bach carries only its shared `room_large`
scene/send, and Grieg contains only its shared room scene/send. The two omitted legacy Chopin
commands have an empty PATCH mask, so they change no AICA register and are
intentionally not emitted. The legacy pre-release files themselves are not a
second supported AFX format: this was a one-time source migration into AFP v4.
