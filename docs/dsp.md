# DSP programming

Use `<aicaflow/dsp.h>` to construct an `afx_dsp_program_t` in C. The helpers
encode one AICA DSP instruction at a time and validate the resource limits
before upload. Install a completed program with `afx_dsp_scene_program()` and
remove it with `afx_dsp_scene_disable()`.

DSP belongs to the loaded scene, not to an individual AFX flow. A flow selects
its existing AICA DSP-send register value; SH4 owns program installation and
return gating. Use `afx_dsp_scene_returns(false)` to audition dry routing
without replacing the program.

`examples/dsp_demo` is the smallest programmatic example. The enDjinn-based
`examples/dsp_effects_player` lets a user audition the built-in C presets with
generated tones, an impulse and the CC0 Wilhelm-scream input. The detailed
operand, memory and scheduling notes live beside the API as inline comments in
`driver/sh4/include/aicaflow/dsp.h`.
