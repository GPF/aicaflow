# Classical music player

This enDjinn player builds and plays three complete classical works:

- Frédéric Chopin — *Prelude in C minor*, Op. 28 No. 20;
- J. S. Bach — *Cello Suite No. 1, Prelude*, BWV 1007;
- Edvard Grieg — *In the Hall of the Mountain King*, Op. 46 No. 4.

Each public-domain score is compiled to its own AFB, then to a small
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
three MIDI files. To choose another General-MIDI SoundFont, keep the standard
piano preset `0:0` and cello preset `0:42`, then override it during the build:

```sh
make -C examples/music_player SOUNDFONT=/path/to/your.sf2
```

The POSIX-shell downloader verifies SHA-256 for every input. The native C
authoring tool resolves MIDI program/bank changes through the selected SF2,
expands its layered regions offline, and writes AFB/AFX/AFC/AFV/AFI without Python.
[`classical.afbm`](classical.afbm) is the editable bank map; it selects PCM16,
PCM8 or ADPCM per declared MIDI-program mapping and can be extended with
additional SoundFont sources. `profiles/*.afp` are generated from the pinned
base AFX files and are deliberately checked in, so an accidental change to a
score, SoundFont or compiler is caught during a reproducible build.
Mutopia marks the selected
[Chopin](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=472),
[Bach](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=517) and
[Grieg](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=1888) scores
as public domain. GeneralUser is an external input with its
[own licence](https://github.com/ad-si/GeneralUser/blob/master/LICENSE.txt);
it is not distributed in this repository.

The player uploads only the selected work's bank, not the complete SoundFont.
For host loading, map
`examples/music_player/cdrom/aicaflow_music_player` as `/pc/`; a CDI build
stages the same assets. Use D-pad to select, A to play/pause, B to stop, and
left/right to seek ten seconds when the AFC sidecar is present.
