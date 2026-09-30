# Testing

`make check` runs the host driver tests and deterministic Python authoring
checks. It deliberately does not require an ARM7 toolchain.

`make firmware-check` is the maintainer check: it rebuilds the ARM7 firmware and
compares its SHA-256 with `firmware/manifest.json`. A release must pass both.

Hardware smoke testing is manual: run quickstart, the DSP demo and tuner on a
Dreamcast, verify one ordinary sample flow, a runtime DSP program and an asset
upload. Record the KOS and firmware revisions with the test result.
