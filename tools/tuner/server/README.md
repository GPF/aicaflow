
# Persistent tuner server

This is a BBA development target, not an application example. Build it from
the repository root with:

```sh
source /opt/toolchains/dc/kos/environ.sh
make tools
```

Load `tools/tuner/server/bin/afx_tuner_server.elf` once with a Dreamcast
loader, then use `tools/tuner/client.py` to upload an AFB, upload/play its
bank-bound AFX control flow, audition a region or change DSP state. `reset`
returns the tuner to a fully empty AICAflow state without relaunching it. The
complete command and memory-lifetime reference is in
[docs/tuner.md](../../../docs/tuner.md).
