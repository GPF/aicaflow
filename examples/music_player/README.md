# Classical music player

This enDjinn player builds and plays three complete classical works:

- Frédéric Chopin — *Prelude in C minor*, Op. 28 No. 20;
- J. S. Bach — *Cello Suite No. 1, Prelude*, BWV 1007;
- Edvard Grieg — *In the Hall of the Mountain King*, Op. 46 No. 4.

Each public-domain score is compiled to one AFB, its small bank-bound AFX control flow, an
optional SH4-only AFC seek index and an AFV spectrum sidecar. Source MIDI and
the SoundFont are not versioned in this repository. Fetch the pinned sources
once, then build:

```sh
source /opt/toolchains/dc/kos/environ.sh
python3 -m pip install mido sf2utils
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

The downloader verifies SHA-256 for every input. Mutopia marks the selected
[Chopin](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=472),
[Bach](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=517) and
[Grieg](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=1888) scores
as public domain. GeneralUser is an external input with its
[own licence](https://github.com/ad-si/GeneralUser/blob/master/LICENSE.txt);
it is not distributed in this repository.

The player uses a per-song bank, so the full SoundFont is never uploaded to
AICA. For host loading, map
`examples/music_player/cdrom/aicaflow_music_player` as `/pc/`; a CDI build
stages the same assets. Use D-pad to select, A to play/pause, B to stop, and
left/right to seek ten seconds when the AFC sidecar is present.
