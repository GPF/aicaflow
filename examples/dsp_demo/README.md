# Programmable DSP demo

This self-contained Dreamcast demo compiles four short sine hits with MIDI CC91
send enabled, then installs the public `pingpong` DSP program. Each hit produces
alternating 180 ms stereo echoes from AICA's DSP delay RAM.

```sh
source /opt/toolchains/dc/kos/environ.sh
make -C examples/dsp_demo
make -C examples/dsp_demo bin/aicaflow_dsp_demo.cdi
```

The ping-pong program is assembled at runtime with the public C API; no
prebuilt DSP image is required.
