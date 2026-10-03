# AICAflow

AICAflow is a Dreamcast audio runtime and native authoring toolkit for the
Yamaha AICA. Native C tools turn MIDI/SoundFonts, raw PCM, N64 CSeq/ALBank or
Sega MultiPCM VGM/VGZ into sample banks and timed AICA register flows.
On Dreamcast, SH-4 code owns AICA RAM, validation,
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

`make compiler` builds the native C authoring tools; see the
[tool inventory](tools/README.md) and [authoring workflow](docs/authoring.md).
Examples author their assets without Python. The tuner host client uses Python
and the developer test suite uses Python reference checks; neither adds a
Python interpreter to the Dreamcast runtime.

## Files and ownership

| File | Purpose | Used where |
| --- | --- | --- |
| `.afb` | Encoded sample bank | Payload in AICA RAM |
| `.afx` | Timed register commands and reusable note setups | Image in AICA RAM |
| `.afc` | Optional seek checkpoints | SH4 RAM only |
| `.afv` | Optional visualizer animation | Player only |
| `.afi` | Optional binary sample catalog | SH4 code only |
| `.afbm` | SoundFont/MIDI bank-building map | Offline |
| `.afp` | Timbre, DSP and performance adjustments | Offline |
| `.afsfx` | Application-owned SFX grouping/residency map | Offline; current reader in DKR |

Several flows can share one resident bank. A flow always binds to exactly one
bank; it does not contain samples or look them up by instrument name at runtime.
An AFP rewrites ordinary AFX commands, not samples. An AFC is not the control
stream: it is separate SH4-only seeking data. See the
[format reference](docs/specs/assets.md) for layouts, bindings and limits.

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

The reusable CSeq/ALBank and MultiPCM importers are included. DKR's game
integration, ROM extraction, SFX residency policy and bonus soundtrack player
live in the DKR repository. Game ROMs, extracted Nintendo/Sega samples and
soundtracks are not distributed here. OoT's distinct AudioSeq reader remains
experimental research code, not a supported mode of `afx_n64`.
