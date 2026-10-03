# Classical music player

This enDjinn player builds and plays three complete classical works:

- Frédéric Chopin — *Nocturne in C-sharp minor*, Op. 27 No. 1;
- J. S. Bach — *Cello Suite No. 1, Prelude*, BWV 1007;
- Edvard Grieg — *In the Hall of the Mountain King*, Op. 46 No. 4.

Each score is compiled to its own AFB, then to a small
bank-bound AFX control flow with optional SH4-only AFC seek indexes, AFV
spectrum sidecars, and both compact and named AFI SH4 sample catalogs. This
makes high-quality PCM practical while only one bank is resident at a time.
Chopin and Bach explicitly use PCM16. Grieg uses the C author's deterministic
per-sample `auto` policy for exposed pizzicato, percussion and woodwinds:
ADPCM is retained only when it clears both full-sample and attack-window SNR
checks, otherwise PCM8 is used. The sustained string and brass layers remain
ADPCM, keeping its reverb scene comfortably inside the AICA RAM arena. Bach uses
`room_large`, Grieg uses `room`, and Chopin deliberately remains
dry. Those choices and the DSP-send amount live
in their respective tracked `.afp` profiles; the `.afbm` stays concerned only
with sample selection and coding. Third-party MIDI and SoundFont inputs are not
versioned in this repository; the small, user-authored Bach performance MIDI is
the exception. Fetch the pinned external sources once, then build:

```sh
source /opt/toolchains/dc/kos/environ.sh
make -C examples/music_player fetch-assets
make -C examples/music_player
```

`GeneralUser-GS.sf2` is fetched from its public upstream mirror and is only an
input dependency; the generated AFB files contain only samples selected by the
three MIDI files. The map deliberately has separate piano and cello sources.
Thus a focused, better piano SF2 can improve Chopin without claiming that it
is also a cello library:

```sh
make -C examples/music_player \
  PIANO_SOUNDFONT=/path/to/piano.sf2 \
  CELLO_SOUNDFONT=/path/to/cello.sf2
```

Either override may be used on its own. An alternate source deliberately
bypasses the checked-in `.afp` profiles: their hashes bind them to the default
generated AFX files. It therefore gives a dry, authored-tempo comparison build
rather than silently applying a profile to different sample/setup data. Rebuild
with the default sources to restore the tracked performance profiles.

The default Bach build uses the checked-in, humanised performance MIDI
(`performance/bach-bwv1007-prelude-performance.mid`) derived from the fetched
public-domain Mutopia score. It is intentionally source material: its rubato
and note timing are reproducible MIDI data rather than hidden AFX edits. It
is SHA-256 checked by the build (`3455b959b496302bd784cde858c81808712c69c0af84b3aae062866733071599`). It
uses Ethan Winer's royalty-free *Cello Solo* SoundFont's stereo pair at
27 kHz PCM16, leaving room for the Bach room profile.
The default Grieg source is Hans-Joachim Roeder's full type-1 GM orchestration,
fetched from MidiCities with its SHA-256 verified. The map preserves piccolo,
flute, clarinet, bassoon, pizzicato/ensemble strings, brass and timpani instead
of collapsing them to piano. Supply a different orchestral MIDI and SoundFont
with this invocation; neither third-party asset is redistributed by AICAflow:

```sh
make -C examples/music_player \
  GRIEG_MIDI='/path/to/In The Hall of The Mountain King (orchestra).mid' \
  ORCHESTRA_SOUNDFONT='/path/to/orchestra-gm.sf2'
```

`ORCHESTRA_CHANNEL` follows the same source-channel rule as the piano and
cello variables. Encoding is an intentional, tracked policy in
[`classical.afbm`](classical.afbm), whose `auto` entries choose PCM8 or ADPCM
per resolved sample. The default Grieg bank uses the left source channel and
leaves AICA RAM for its tracked-at-build-time `room` profile (DSP send 112).
An orchestral replacement receives a freshly derived profile rather than
applying the tracked one to different setup data.

For a piano-only audition, Salamander Grand Piano is a high-detail source, but
the full public-domain SF2 is about 1.27 GB and so is intentionally not fetched
by this example. Point `PIANO_SOUNDFONT` at a locally obtained copy. The smaller
Salamander C5 Light SF2 is suitable for personal listening but has a
personal-use licence, so it is likewise not a project dependency. Replace the
verified Cello Solo source with any suitably licensed `CELLO_SOUNDFONT`; the
map can combine the two without any runtime change.

Those banks have much longer, layered piano recordings than fit as PCM16 in
AICA RAM. The C5 Light comparison fits as PCM8 while retaining its source
layers:

```sh
make -C examples/music_player \
  PIANO_SOUNDFONT=sources/SalC5Light2.sf2 PIANO_FORMAT=pcm8 PIANO_CHANNEL=left
```

`PIANO_FORMAT` and `CELLO_FORMAT` are passed directly to their `.afbm` maps
and accept `pcm16`, `pcm8`, `adpcm`, or `auto`. Grieg's mixed policy is kept
in the checked-in map so it remains reproducible rather than becoming a hidden
make-variable choice.
`PIANO_CHANNEL` and `CELLO_CHANNEL` default to `stereo`; the compact default
Grieg mapping selects `left`. `left` or `right` select one side of a linked
stereo pair and centre it on AICA, halving the paired sample memory when a
detailed source would otherwise not fit.

The POSIX-shell downloader verifies SHA-256 for every input. The native C
authoring tool resolves MIDI program/bank changes through the selected SF2,
expands its layered regions offline, and writes AFB/AFX/AFC/AFV/AFI.
[`classical.afbm`](classical.afbm) is the editable bank map; it selects PCM16,
PCM8 or ADPCM per declared MIDI-program mapping and can be extended with
additional SoundFont sources. `auto` makes its format decision for every
resolved sample, not merely for a whole instrument program. `profiles/*.afp`
are generated from the pinned base AFX files and are deliberately checked in,
so an accidental change to a score, SoundFont or compiler is caught during a
reproducible build.
The Chopin composition is public domain; this particular MIDI performance is
credited to Bernd Krueger and licensed under
[CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/), as recorded
by [its source listing](https://piano.center/midi/nocturne-op-27-no-1-in-c-sharp-minor).
It is fetched from a pinned mirror of that performance. Mutopia marks the
selected [Bach](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=517)
score as public domain. Grieg's generated orchestration is credited to
Hans-Joachim Roeder and remains an external input obtained from
[MidiCities](https://midicities.com/geocities-browse?title=4); it is not
redistributed by this repository. GeneralUser is an external input with its
[own licence](https://github.com/ad-si/GeneralUser/blob/master/LICENSE.txt);
it is not distributed in this repository.

The player uploads only the selected work's bank, not the complete SoundFont.
For host loading, map
`examples/music_player/cdrom/aicaflow_music_player` as `/pc/`; a CDI build
stages the same assets. Use D-pad to select, A to play/pause, B to stop, and
left/right to seek ten seconds when the AFC sidecar is present.

## Hardware frame test

The production loader can be exercised without controller input. It loads and
plays every work through the normal asynchronous AFB/AFV path, verifies that
the visualizer starts, prints `CLASSICAL PLAYER FRAME TEST PASS`, then exits:

```sh
source /Users/drxl/projects/dreamcast/enDjinn/environ.sh
make -C examples/music_player frame-test
kos-tool -f -t "$DCTOOL_HOST" \
  -m "$PWD/examples/music_player/cdrom/aicaflow_music_player" \
  -x "$PWD/examples/music_player/bin/aicaflow_music_player.elf"
```

`frame-test` intentionally leaves a self-terminating ELF. Run
`make -C examples/music_player -B` afterwards to restore the interactive
player before listening or recording.
