# Tools

`make compiler` builds the supported native C authoring tools:

- `build/afx_compile` — one MIDI plus PCM zones or one SoundFont to AFB/AFX,
  with matching AFC/AFV sidecars.
- `build/afx_bank` — an editable `.afbm` map to one shared AFB and a flow
  plus sidecars for each declared song.
- `build/afx_profile` — initialise, inspect and apply an `.afp` performance
  profile to an AFX/AFC pair.
- `build/afx_demo_assets` — deterministic small assets for examples/tests.

The full workflows, input syntax and sidecar roles are in
[Authoring](../docs/authoring.md) and
[Assets and sidecars](../docs/specs/assets.md).

`tuner/` contains the resident Dreamcast tuner (`server/`) and its small host
client (`client.py`). It is a development target, not an application example;
see [Tuner](../docs/tuner.md) for commands, memory lifetime and reset behavior.

`test/` contains fixtures and checks. `research/` contains the older Python
experiments and reference implementation. They remain useful for comparison
and investigation, but examples and the release authoring path have no Python
dependency.
