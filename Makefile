SHELL := /bin/bash

KOS_ENV ?= /opt/toolchains/dc/kos/environ.sh
EXAMPLES := quickstart dsp_demo dynamic_sfx dsp_effects_player music_player
TOOLS := tuner_server
C_COMPILER := build/afx_compile_c
C_COMPILER_TEST := build/test_afx_compile_c

.PHONY: all examples tools check compiler firmware firmware-check clean

all: examples tools

compiler: $(C_COMPILER)

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
	./$(C_COMPILER) examples/quickstart/build/fixture.mid --zones tools/fixtures/c_fixture.zones \
	"$$task_tmp/fixture.afb" "$$task_tmp/fixture.afx" && driver/build/afx_validate "$$task_tmp/fixture.afx"
	python3 tools/test_afx_adpcm.py
	python3 tools/test_afx_midi.py
	python3 tools/test_afx_perf.py
	python3 tools/test_afx_sf2.py
	python3 tools/test_make_fixture_midi.py

$(C_COMPILER): tools/afx_compile_c.c tools/afx_compile_c.h tools/afx_compile_c_cli.c tools/afx_midi_c.c tools/afx_midi_c.h driver/common/codec.c driver/include/aicaflow/codec.h driver/include/aicaflow/protocol.h
	mkdir -p build
	clang -std=c11 -O2 -Wall -Wextra -Werror -Idriver/include tools/afx_compile_c.c tools/afx_midi_c.c tools/afx_compile_c_cli.c -lm -o $@

$(C_COMPILER_TEST): tools/afx_compile_c.c tools/afx_compile_c.h tools/afx_midi_c.c tools/afx_midi_c.h tools/test_afx_compile_c.c driver/common/codec.c driver/include/aicaflow/codec.h driver/include/aicaflow/protocol.h
	mkdir -p build
	clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Idriver/include tools/afx_compile_c.c tools/afx_midi_c.c tools/test_afx_compile_c.c driver/common/codec.c -lm -o $@

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
	rm -f $(C_COMPILER) $(C_COMPILER_TEST)
