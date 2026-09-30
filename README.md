# AICAflow

AICAflow is a Dreamcast AICA runtime and authoring toolkit. It runs prepared
AFX control flows against contiguous AFB sample banks with a bounded ARM7
executor and SH4-side resource ownership.

## Release contract

- One sampled-flow model: one AFB plus one AFX. AFX never embeds samples.
- One versioned firmware image: `firmware/aicaflow.drv`.
- ARM7 executes prepared commands only. SH4 owns allocation, bank binding,
  instances, seeking, dynamic register updates and DSP-scene installation.
- AFB/AFX validation rejects incompatible banks and malformed relocations
  before an instance can play.

## Build an example

```sh
git clone --recurse-submodules https://github.com/dfchil/aicaflow.git
cd aicaflow
source /opt/toolchains/dc/kos/environ.sh
make examples
```

The checked-in firmware means this needs no ARM7 compiler. Run `make check` for
host validation; maintainers with the ARM toolchain run `make firmware-check`
to reproduce the release image.

`make compiler` builds the native C authoring tools. They emit AFB, AFX, AFC
and AFV from MIDI plus raw PCM zones or an SF2; `afx_bank_c` builds one shared
AFB for a declared list of songs. Python lives only in `tools/research` and
`tools/test` as optional reference tooling.

## Included examples

- `quickstart` — minimal generated AFB/AFX playback.
- `dsp_demo` — runtime DSP program construction in C.
- `dsp_effects_player` — interactive DSP-preset audition player.
- `dynamic_sfx` — SH4-controlled pitch, position and intensity changes.
- `music_player` — three reproducibly fetched classical MIDI/SoundFont demonstrations.

The persistent BBA tuner is a development tool at `tools/tuner/server`; build
it with `make tools`.

Interactive examples use the pinned `third_party/enDjinn` submodule. The core
driver does not. `music_player` downloads its declared MIDI and SoundFont
inputs on its first build; see its README to supply a different SoundFont.

## Documentation

Start with [getting started](docs/getting-started.md), then read the
[integration](docs/integration.md), [lifetime](docs/lifetime.md),
[format](docs/formats.md), [authoring](docs/authoring.md) and
[DSP](docs/dsp.md) guides. Asset-specific licensing is in
[ASSET_LICENSES.md](ASSET_LICENSES.md).

DKR-specific integration, Nintendo 64 importers and other game-derived work
are intentionally not part of this generic release.
