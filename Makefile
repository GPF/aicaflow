SHELL := /bin/bash

KOS_ENV ?= /opt/toolchains/dc/kos/environ.sh
EXAMPLES := quickstart dsp_demo dynamic_sfx dsp_effects_player music_player
TOOLS := tuner/server
C_COMPILER := build/afx_compile_c
C_COMPILER_TEST := build/test_afx_compile_c
DEMO_ASSETS := build/afx_demo_assets
BANK_COMPILER := build/afx_bank_c

.PHONY: all examples tools check compiler firmware firmware-check clean

all: examples tools

compiler: $(C_COMPILER) $(DEMO_ASSETS) $(BANK_COMPILER)

examples:
	@for example in $(EXAMPLES); do \
		source $(KOS_ENV) && $(MAKE) -C examples/$$example || exit $$?; \
	done

tools:
	@for tool in $(TOOLS); do \
		source $(KOS_ENV) && $(MAKE) -C tools/$$tool || exit $$?; \
	done

check: $(C_COMPILER) $(C_COMPILER_TEST)
	$(MAKE) -C driver smoke
	./$(C_COMPILER_TEST)
	@task_tmp=$$(mktemp -d); trap 'rm -rf "$$task_tmp"' EXIT; \
	python3 tools/research/make_fixture_midi.py "$$task_tmp/fixture.mid" && \
	./$(C_COMPILER) "$$task_tmp/fixture.mid" --zones tools/test/fixtures/c_fixture.zones \
	"$$task_tmp/fixture.afb" "$$task_tmp/fixture.afx" && driver/build/afx_validate "$$task_tmp/fixture.afx"
	PYTHONPATH=tools/research:tools/tuner python3 tools/test/test_afx_adpcm.py
	PYTHONPATH=tools/research:tools/tuner python3 tools/test/test_afx_midi.py
	PYTHONPATH=tools/research:tools/tuner python3 tools/test/test_afx_perf.py
	PYTHONPATH=tools/research:tools/tuner python3 tools/test/test_afx_sf2.py
	PYTHONPATH=tools/research:tools/tuner python3 tools/test/test_make_fixture_midi.py
	PYTHONPATH=tools/tuner python3 tools/test/test_afx_tuner_client.py
	PYTHONPATH=tools/tuner python3 tools/test/test_afx_tuner_example.py

$(C_COMPILER): tools/author/afx_compile_c.c tools/author/afx_compile_c.h tools/author/afx_compile_c_cli.c tools/author/afx_midi_c.c tools/author/afx_midi_c.h tools/author/afx_sample_c.c tools/author/afx_sample_c.h tools/author/afx_sf2_c.c tools/author/afx_sf2_c.h tools/author/afx_ya2beam.c driver/common/codec.c driver/include/aicaflow/codec.h driver/include/aicaflow/protocol.h
	mkdir -p build
	clang -std=c11 -O2 -Wall -Wextra -Werror -Idriver/include tools/author/afx_compile_c.c tools/author/afx_midi_c.c tools/author/afx_sample_c.c tools/author/afx_sf2_c.c tools/author/afx_ya2beam.c tools/author/afx_compile_c_cli.c -lm -o $@

$(C_COMPILER_TEST): tools/author/afx_compile_c.c tools/author/afx_compile_c.h tools/author/afx_midi_c.c tools/author/afx_midi_c.h tools/author/afx_sample_c.c tools/author/afx_sample_c.h tools/author/afx_ya2beam.c tools/test/test_afx_compile_c.c driver/common/codec.c driver/include/aicaflow/codec.h driver/include/aicaflow/protocol.h
	mkdir -p build
	clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Idriver/include -Itools/author tools/author/afx_compile_c.c tools/author/afx_midi_c.c tools/author/afx_sample_c.c tools/author/afx_ya2beam.c tools/test/test_afx_compile_c.c driver/common/codec.c -lm -o $@

$(DEMO_ASSETS): tools/author/afx_demo_assets.c tools/author/afx_compile_c.c tools/author/afx_compile_c.h driver/include/aicaflow/protocol.h
	mkdir -p build
	clang -std=c11 -O2 -Wall -Wextra -Werror -Idriver/include tools/author/afx_demo_assets.c tools/author/afx_compile_c.c -lm -o $@

$(BANK_COMPILER): tools/author/afx_bank_c.c tools/author/afx_compile_c.c tools/author/afx_compile_c.h tools/author/afx_midi_c.c tools/author/afx_midi_c.h tools/author/afx_sample_c.c tools/author/afx_sample_c.h tools/author/afx_sf2_c.c tools/author/afx_sf2_c.h tools/author/afx_ya2beam.c driver/include/aicaflow/codec.h driver/include/aicaflow/protocol.h
	mkdir -p build
	clang -std=c11 -O2 -Wall -Wextra -Werror -Idriver/include tools/author/afx_bank_c.c tools/author/afx_compile_c.c tools/author/afx_midi_c.c tools/author/afx_sample_c.c tools/author/afx_sf2_c.c tools/author/afx_ya2beam.c -lm -o $@

firmware:
	source $(KOS_ENV) && $(MAKE) -C driver/arm7

firmware-check: firmware
	@expected=$$(python3 -c 'import json; print(json.load(open("firmware/manifest.json"))["sha256"])'); \
	actual=$$(shasum -a 256 firmware/aicaflow.drv | awk '{print $$1}'); \
	test "$$actual" = "$$expected" || { echo "firmware/aicaflow.drv differs from its release manifest" >&2; exit 1; }

clean:
	@for example in $(EXAMPLES); do $(MAKE) -C examples/$$example clean; done
	@for tool in $(TOOLS); do $(MAKE) -C tools/$$tool clean; done
	$(MAKE) -C driver clean
	rm -f $(C_COMPILER) $(C_COMPILER_TEST) $(DEMO_ASSETS) $(BANK_COMPILER)
