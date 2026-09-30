# DSP programming

Use `<aicaflow/dsp.h>` to construct an `afx_dsp_program_t` in C. The helpers
encode one AICA DSP instruction at a time and validate the resource limits
before upload. Install a completed program with `afx_dsp_scene_program()` and
remove it with `afx_dsp_scene_disable()`.

DSP belongs to the loaded scene, not to an individual AFX flow. A flow selects
its existing AICA DSP-send register value; SH4 owns program installation and
return gating. Use `afx_dsp_scene_returns(false)` to audition dry routing
without replacing the program.

The SH4 also owns the AICA asset arena. The DSP ring is the only variable
top-of-arena reservation: a program without `MRD`/`MWT` delay instructions
reserves 0 bytes; RBL 0, 1, 2 and 3 reserve 16, 32, 64 and 128 KiB
respectively. All AFB data is 32-byte aligned and is allocated below that
reservation. Install a memory-using scene before loading its AFB; an attempted
ring expansion that would overlap a live bank fails safely. Disabling the
scene returns that range to the ordinary allocator.

For authored music, keep the scene preset and its DSP-send amount in the
offline `.afp` profile. The profile rewrite puts the send in the derived AFX;
the player installs the named scene before loading the matching AFB. Neither
the profile nor an allocator policy is interpreted by ARM7.

`examples/dsp_demo` is the smallest programmatic example. The enDjinn-based
`examples/dsp_effects_player` lets a user audition the built-in C presets with
generated tones, an impulse and the CC0 Wilhelm-scream input. The detailed
operand, memory and scheduling notes live beside the API as inline comments in
`driver/sh4/include/aicaflow/dsp.h`.
