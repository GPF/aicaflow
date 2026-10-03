# SH4 integration

There is one runtime path: load a sample bank, bind a control flow, then create
instances. The driver library does not depend on enDjinn. Link
`driver/sh4/libaicaflow_host.a`, add `driver/include` and `driver/sh4/include`
to the include path, and provide the checked-in firmware.

Include `<aicaflow/host.h>` and `<aicaflow/bank.h>`, embed
`firmware/aicaflow.drv`, then initialize AICAflow once:

```c
alignas(32) static const unsigned char firmware[] = {
#embed "firmware/aicaflow.drv"
};

int result = afx_init(firmware, sizeof(firmware));
/* Check result before loading or activating assets. */
```

Load one AFB into an `afx_bank_t`, upload an AFX bound to that bank, then
activate the resulting flow. Call `afx_update()` regularly and recycle finished
instances. The complete checked-in [quickstart](../examples/quickstart/code/main.c)
demonstrates memory inputs and completion waits; these are the corresponding
application steps:

1. Initialize once. Startup/shutdown require other host calls to be quiescent.
2. If using delay-memory DSP, choose/install its ring before filling the arena.
3. Load AFB with `afx_bank_load_file()` or `afx_bank_load_memory()` into a
   zero-initialized `afx_bank_t`.
4. Read the AFX bytes and call `afx_bank_flow_upload()` against that bank.
   Attach an optional AFC with `afx_flow_seek_index_load_file()` **before**
   activating; an active referenced flow cannot replace its seek index.
5. Activate with `afx_instance_activate()`. Apply player gain/tempo and other
   live controls using the returned instance handle.
6. Call `afx_update()` and inspect `afx_instance_status()` regularly. A zero
   return from a lifecycle call means queued, not "ARM7 finished". Check both
   the API result and the later observed state/result.
7. On completion or explicit STOP, wait for DONE/ERROR, queue recycle and wait
   until the old handle is stale. Only then free its flow and release its bank.

Integer APIs generally return zero on success and a negative `afx_result_t`
on failure. Handle/address getters use zero for invalid results. Do not spin
through allocation/queue failures blindly: check memory, pending lifecycle
state, execution budget and the observed ARM7 result. Preserve successfully
created handles until their normal cleanup has completed, including on partial
startup failure. `afx_shutdown()` requires flows, instances and banks released.

## Shared banks and live controls

An uploaded flow retains its bank; an instance retains its flow. Several
different or concurrent flows can share a bank, but each flow binds to just
one. A scene transition should stop/recycle old instances before freeing their
flows/bank, or deliberately keep a shared bank resident. No automatic sample
cache or game-specific bank-swap policy exists in the core driver.

`afx_instance_patch()` addresses a **local channel**, not a physical AICA
channel. Its mask and values are the existing register fields in ascending
field order. Pitch, mix, envelope, pan/filter, LFO and DSP send can be changed
while a voice is running/parked. Host-built SFX protect their immutable sample
binding and reject sample-address/loop changes and raw seek/rebuild. Use a
new bound flow when replacing that sample, not a forged address PATCH.

Runtime lane modifiers are persistent SH4-controlled grouping modifiers
(gain/mute/pan/send) on authored lane maps. An AFP's offline `lanes` entries
are different: they compile into ordinary timed PATCH operations and require
no profile interpreter or runtime random generator.

## Seek, DSP and player metadata

Seeking requires a matching AFC and a paused music instance. SH4 reconstructs
register state, resolves addresses and submits REBUILD. The checkpoint table
stays on SH4; only prepared state uses temporary AICA staging. This is not a
sample-phase/DSP-buffer snapshot. AFV visualization is separate and optional.

One DSP program belongs to the loaded scene. AFX register words choose channel
sends; the application installs/gates the DSP program. An AFP's `describe`
output gives build-time scene/tempo metadata. The runtime does not open AFP,
AFBM or AFSFX and does not infer a preset or tempo from an AFX filename.

For file-backed playback over a host tool, map the actual asset directory as
`/pc` (`-m`); keep that server and computer awake. A successful ELF upload does
not guarantee later scene/song file reads will work over Wi-Fi or after sleep.

See [lifetime.md](lifetime.md) for the ownership rules and
[Assets and sidecars](specs/assets.md) for the asset contract. See
[Runtime ABI](specs/runtime.md) for the SH-4/ARM7 boundary.
