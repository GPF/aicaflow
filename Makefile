SHELL := /bin/bash

KOS_ENV ?= /opt/toolchains/dc/kos/environ.sh
EXAMPLES := quickstart dsp_demo dynamic_sfx tuner_server dsp_effects_player

.PHONY: all examples check firmware firmware-check clean

all: examples

examples:
	@for example in $(EXAMPLES); do \
		source $(KOS_ENV) && $(MAKE) -C examples/$$example || exit $$?; \
	done

check:
	$(MAKE) -C driver smoke
	python3 tools/test_afx_adpcm.py
	python3 tools/test_afx_midi.py
	python3 tools/test_afx_perf.py
	python3 tools/test_afx_sf2.py
	python3 tools/test_make_fixture_midi.py

firmware:
	source $(KOS_ENV) && $(MAKE) -C driver/arm7

firmware-check: firmware
	@expected=$$(python3 -c 'import json; print(json.load(open("firmware/manifest.json"))["sha256"])'); \
	actual=$$(shasum -a 256 firmware/aicaflow.drv | awk '{print $$1}'); \
	test "$$actual" = "$$expected" || { echo "firmware/aicaflow.drv differs from its release manifest" >&2; exit 1; }

clean:
	@for example in $(EXAMPLES); do $(MAKE) -C examples/$$example clean; done
	$(MAKE) -C driver clean
