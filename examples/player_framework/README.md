# AICAflow player framework

`music_player.c` is the shared enDjinn UI and playback lifecycle used by the
music-player examples.  A player supplies its playlist as `songs.h` and a
small set of `PLAYER_*` macros, then includes this file from `code/main.c`.

The framework has no asset generator and no fixed catalog. It can keep one
shared AFB resident (`PLAYER_SHARED_BANK_FILE`) or asynchronously replace a
per-song bank (`PLAYER_SONG_BANK_FILE`). AFX/AFC loading is synchronous;
per-song AFB and AFV loading are stepped through the enDjinn loop. It presents the playlist,
progress bar, seek controls, and spectrum display.  It is intentionally a
source include rather than another runtime library: an enDjinn example already
owns its build and this keeps the framework optional for normal AICAflow users.

The generated playlist supplies real byte sizes, bank/control/visual filenames,
and optional build-time DSP/tempo/gain metadata. `PLAYER_LIST_BYTES` and
`PLAYER_LIST_HEADING` choose the displayed size (for example SH4-only AFC KiB,
not AICA usage). These values must come from actual authored outputs, not zero
placeholders. The info line separately reports AICA image/sample/setup costs;
with a shared bank, per-song sample bytes can be zero without the music bank
being absent. `SAV` is the estimate `(NOTE count - setup count) * 36`, not a
measured file diff against an independently compiled untemplated stream.

The player constructs DSP from the build metadata and applies instance tempo;
it does not interpret AFP, AFBM or AFSFX on Dreamcast. AFV is a note-energy
animation, not a captured FFT. The enDjinn main loop handles rendering/input,
including the START+A+B+X+Y exit chord. See the
[classical player](../music_player/README.md) and the
[format reference](../../docs/specs/assets.md) for reproducible assets.
