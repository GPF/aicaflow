# ARM7 timed executor

This ABI 3 firmware owns Timer A, but its FIQ only increments the reserved clock
and reloads the timer. Bounded normal-context code drains IPC and executes due
stream operations. It advertises bootstrap, lifecycle and playback capabilities.

The linker reserves low code, high writable data/BSS, all five banked stacks,
shared status/IPC/context maps and the permanent 128 KiB DSP ring. Startup copies
initialized data, clears BSS and fills stack watermarks before entering C.
The host validates the embedded layout manifest before resetting/uploading.

See [the numeric/wire contract and checks](../CONTRACT.md), the
[accepted specification](../../AICAFLOW_SPEC.md) and
[delivery gates](../../ACCEPTANCE_PLAN.md). The old 0xb000 stack, wait-counter
interpreter and AFLW format are not current implementations.

Build after sourcing your KOS environment:

```sh
make -C driver/arm7
make -C driver layout
```

The layout check validates the built manifest and accounts for all 2 MiB.
The finite [hardware smoke test](../../RECORDING.md) verifies bootstrap startup,
G2 status reads, Timer A advancement, a compiler-generated MIDI-to-AFX fixture,
an autonomous relocated tone with a timed pan PATCH, PARK/SH-4 PATCH/STOP and
clean loader return on the primary console. Initial main-stack free space was
2032/2048 bytes; all four unused exception stacks retained 256/256. Interrupt
load, timing stress and worst-case running-stack usage remain unverified.

Flycast v2.6's ARM recompiler silently ignores `msr cpsr_c` writes. Startup
and FIQ enable therefore use register-form `msr cpsr_cf`: startup flags are
unused, and FIQ enable preserves the flags read by `mrs`. Without this,
Timer A counts and the ARM heartbeat advances, but the software clock stays
at zero; seeking can sound notes while timed playback remains stuck.
See [Flycast's MSR decoder](https://github.com/flyinghead/flycast/blob/v2.6/core/hw/arm7/arm7_rec.cpp#L148-L188).

A bounded check boots the actual firmware and verifies that both its clock
and heartbeat advance in three SH-4-timed intervals:

```sh
source /opt/toolchains/dc/kos/environ.sh
PATH="$PATH:/opt/toolchains/dc/bin" make -C driver timer-check
/Applications/Flycast.app/Contents/MacOS/Flycast -config config:Debug.SerialConsoleEnabled=yes,config:Dreamcast.AutoLoadState=no,config:Dreamcast.AutoSaveState=no,config:UseReios=yes driver/build/check_timer.cdi
```

Expect `AFX_TIMER PASS` in the serial output. This checks clock liveness,
not audio quality or hardware clock calibration.
