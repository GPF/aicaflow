# AICAflow player framework

`music_player.c` is the shared enDjinn UI and playback lifecycle used by the
music-player examples.  A player supplies its playlist as `songs.h` and a
small set of `PLAYER_*` macros, then includes this file from `code/main.c`.

The framework has no asset generator and no fixed catalog.  It loads one AFB
and each selected AFX/AFC/AFV sidecar asynchronously, presents the playlist,
progress bar, seek controls, and spectrum display.  It is intentionally a
source include rather than another runtime library: an enDjinn example already
owns its build and this keeps the framework optional for normal AICAflow users.
