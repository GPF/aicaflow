# AICAflow

AICAflow is a Dreamcast audio runtime and native authoring toolkit for the
Yamaha AICA. Offline tools turn MIDI, PCM or SoundFonts into sample banks and
timed AICA register flows. On Dreamcast, SH-4 code owns AICA RAM, validation,
bank binding, flow instances, seeking, live parameter changes and DSP scenes;
the ARM7 firmware is a bounded executor for already-resolved commands. This
keeps file parsing, sample selection and expensive policy off the audio CPU.

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

Start with the [documentation index](docs/README.md). It links the getting
started, integration, authoring, DSP and tuner guides, plus the normative
[runtime](docs/specs/runtime.md) and [asset/sidecar](docs/specs/assets.md)
specifications and the [AICA memory layout](docs/memory.md). Asset-specific
licensing is in [ASSET_LICENSES.md](ASSET_LICENSES.md).

DKR-specific integration, Nintendo 64 importers and other game-derived work
are intentionally not part of this generic release.
