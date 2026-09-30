# Classical music player

This enDjinn player builds and plays three complete classical works:

- Frédéric Chopin — *Nocturne in C-sharp minor*, Op. 27 No. 1;
- J. S. Bach — *Cello Suite No. 1, Prelude*, BWV 1007;
- Edvard Grieg — *In the Hall of the Mountain King*, Op. 46 No. 4.

Each score is compiled to its own AFB, then to a small
bank-bound AFX control flow with optional SH4-only AFC seek indexes, AFV
spectrum sidecars, and both compact and named AFI SH4 sample catalogs. This
makes PCM16 affordable for all three works while only one bank is resident at a
time. Bach uses `room_warm`, Grieg uses `room`, and Chopin deliberately remains
dry. Those choices and the DSP-send amount live
in their respective tracked `.afp` profiles; the `.afbm` stays concerned only
with sample selection and coding. Source MIDI and
the SoundFont are not versioned in this repository. Fetch the pinned sources
once, then build:

```sh
source /opt/toolchains/dc/kos/environ.sh
make -C examples/music_player fetch-assets
make -C examples/music_player
```

`GeneralUser.sf2` is fetched from its public upstream mirror and is only an
input dependency; the generated AFB files contain only samples selected by the
three MIDI files. The map deliberately has separate piano and cello sources.
Thus a focused, better piano SF2 can improve Chopin and Grieg without claiming
that it is also a cello library:

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

For a piano-only audition, Salamander Grand Piano is a high-detail source, but
the full public-domain SF2 is about 1.27 GB and so is intentionally not fetched
by this example. Point `PIANO_SOUNDFONT` at a locally obtained copy. The smaller
Salamander C5 Light SF2 is suitable for personal listening but has a
personal-use licence, so it is likewise not a project dependency. Choose a
separately licensed cello SF2 for `CELLO_SOUNDFONT`; the map can combine the two
without any runtime change.

Those banks have much longer, layered piano recordings than fit as PCM16 in
AICA RAM. The C5 Light comparison fits as PCM8 while retaining its source
layers:

```sh
make -C examples/music_player \
  PIANO_SOUNDFONT=sources/SalC5Light2.sf2 PIANO_FORMAT=pcm8 PIANO_CHANNEL=left
```

`PIANO_FORMAT` and `CELLO_FORMAT` are passed directly to their respective
`.afbm` mappings and accept `pcm16`, `pcm8`, `adpcm`, or `auto`.
`PIANO_CHANNEL`/`CELLO_CHANNEL` default to `stereo`; `left` or `right` select
one side of a linked stereo pair and centre it on AICA, halving the paired
sample memory when a detailed source would otherwise not fit.

The POSIX-shell downloader verifies SHA-256 for every input. The native C
authoring tool resolves MIDI program/bank changes through the selected SF2,
expands its layered regions offline, and writes AFB/AFX/AFC/AFV/AFI without Python.
[`classical.afbm`](classical.afbm) is the editable bank map; it selects PCM16,
PCM8 or ADPCM per declared MIDI-program mapping and can be extended with
additional SoundFont sources. `profiles/*.afp` are generated from the pinned
base AFX files and are deliberately checked in, so an accidental change to a
score, SoundFont or compiler is caught during a reproducible build.
The Chopin composition is public domain; this particular MIDI performance is
credited to Bernd Krueger and licensed under
[CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/), as recorded
by [its source listing](https://piano.center/midi/nocturne-op-27-no-1-in-c-sharp-minor).
It is fetched from a pinned mirror of that performance. Mutopia marks the
selected [Bach](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=517) and
[Grieg](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=1888) scores
as public domain. GeneralUser is an external input with its
[own licence](https://github.com/ad-si/GeneralUser/blob/master/LICENSE.txt);
it is not distributed in this repository.

The player uploads only the selected work's bank, not the complete SoundFont.
For host loading, map
`examples/music_player/cdrom/aicaflow_music_player` as `/pc/`; a CDI build
stages the same assets. Use D-pad to select, A to play/pause, B to stop, and
left/right to seek ten seconds when the AFC sidecar is present.
