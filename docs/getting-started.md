# Getting started

AICAflow runs prepared AFB/AFX assets on Dreamcast AICA hardware.  The package
includes a verified ARM7 firmware image, so normal example builds need only a
KallistiOS SH4 environment, Python 3 and the Python packages `mido` and
`sf2utils`.

```sh
git clone --recurse-submodules https://github.com/dfchil/aicaflow.git
cd aicaflow
source /opt/toolchains/dc/kos/environ.sh
python3 -m pip install mido sf2utils
make examples
```

`firmware/aicaflow.drv` is embedded by every example.  It is validated at
runtime and does not require an ARM7 compiler.  Maintainers with the full KOS
toolchain can regenerate and verify it with `make firmware-check`.

The `third_party/enDjinn` submodule is used only by the interactive examples.
The driver itself has no enDjinn dependency.
