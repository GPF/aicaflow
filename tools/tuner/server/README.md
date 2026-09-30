
# Persistent tuner server

This is a BBA development target, not an application example. Build it from
the repository root with:

```sh
source /opt/toolchains/dc/kos/environ.sh
make tools
```

Load `tools/tuner/server/bin/afx_tuner_server.elf` once with a Dreamcast
loader, then use `tools/tuner/client.py` to upload an AFB, upload/play its
bank-bound AFX control flow, audition a region or change DSP state. The wire
protocol is described in [docs/tuner.md](../../../docs/tuner.md).
