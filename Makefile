SHELL := /bin/bash

KOS_ENV ?= /opt/toolchains/dc/kos/environ.sh
EXAMPLES := quickstart dsp_demo dynamic_sfx dsp_effects_player music_player
TOOLS := tuner/server
C_COMPILER := build/afx_compile
C_COMPILER_TEST := build/test_afx_compile
DEMO_ASSETS := build/afx_demo_assets
BANK_COMPILER := build/afx_bank
PROFILE_COMPILER := build/afx_profile

.PHONY: all examples tools check compiler firmware firmware-check clean

all: examples tools

compiler: $(C_COMPILER) $(DEMO_ASSETS) $(BANK_COMPILER) $(PROFILE_COMPILER)

examples:
	@for example in $(EXAMPLES); do \
		source $(KOS_ENV) && $(MAKE) -C examples/$$example || exit $$?; \
	done

tools:
	@for tool in $(TOOLS); do \
		source $(KOS_ENV) && $(MAKE) -C tools/$$tool || exit $$?; \
	done

check: $(C_COMPILER) $(C_COMPILER_TEST) $(PROFILE_COMPILER)
	$(MAKE) -C driver smoke
	./$(C_COMPILER_TEST)
	@task_tmp=$$(mktemp -d); trap 'rm -rf "$$task_tmp"' EXIT; \
	python3 tools/research/make_fixture_midi.py "$$task_tmp/fixture.mid" && \
	./$(C_COMPILER) "$$task_tmp/fixture.mid" --zones tools/test/fixtures/c_fixture.zones \
	"$$task_tmp/fixture.afb" "$$task_tmp/fixture.afx" && driver/build/afx_validate "$$task_tmp/fixture.afx"
	@task_tmp=$$(mktemp -d); trap 'rm -rf "$$task_tmp"' EXIT; \
	python3 tools/research/make_fixture_midi.py "$$task_tmp/fixture.mid" && \
	./$(C_COMPILER) "$$task_tmp/fixture.mid" --zones tools/test/fixtures/c_fixture.zones \
	"$$task_tmp/fixture.afb" "$$task_tmp/fixture.afx" && \
	./$(PROFILE_COMPILER) init "$$task_tmp/fixture.afx" "$$task_tmp/fixture.afp" room 112 && \
	python3 -m json.tool "$$task_tmp/fixture.afp" >/dev/null && \
	./$(PROFILE_COMPILER) describe "$$task_tmp/fixture.afx" "$$task_tmp/fixture.afp" | grep -qx 'room 112' && \
	./$(PROFILE_COMPILER) apply "$$task_tmp/fixture.afx" "$$task_tmp/fixture.afc" "$$task_tmp/fixture.afp" \
	"$$task_tmp/profiled.afx" "$$task_tmp/profiled.afc" && driver/build/afx_validate "$$task_tmp/profiled.afx"
	PYTHONPATH=tools/research:tools/tuner python3 tools/test/test_afx_adpcm.py
	PYTHONPATH=tools/research:tools/tuner python3 tools/test/test_afx_midi.py
	PYTHONPATH=tools/research:tools/tuner python3 tools/test/test_afx_sf2.py
	PYTHONPATH=tools/research:tools/tuner python3 tools/test/test_make_fixture_midi.py
	PYTHONPATH=tools/tuner python3 tools/test/test_afx_tuner_client.py
	PYTHONPATH=tools/tuner python3 tools/test/test_afx_tuner_example.py
	PYTHONPATH=tools/research python3 tools/test/test_afx_n64.py

$(C_COMPILER): tools/author/afx_compile_c.c tools/author/afx_compile_c.h tools/author/afx_compile_c_cli.c tools/author/afx_midi_c.c tools/author/afx_midi_c.h tools/author/afx_sample_c.c tools/author/afx_sample_c.h tools/author/afx_sf2_c.c tools/author/afx_sf2_c.h tools/author/afx_ya2beam.c driver/common/codec.c driver/include/aicaflow/codec.h driver/include/aicaflow/protocol.h
	mkdir -p build
	clang -std=c11 -O2 -Wall -Wextra -Werror -Idriver/include tools/author/afx_compile_c.c tools/author/afx_midi_c.c tools/author/afx_sample_c.c tools/author/afx_sf2_c.c tools/author/afx_ya2beam.c tools/author/afx_compile_c_cli.c driver/common/codec.c -lm -o $@

$(C_COMPILER_TEST): tools/author/afx_compile_c.c tools/author/afx_compile_c.h tools/author/afx_midi_c.c tools/author/afx_midi_c.h tools/author/afx_sample_c.c tools/author/afx_sample_c.h tools/author/afx_ya2beam.c tools/test/test_afx_compile_c.c driver/common/codec.c driver/include/aicaflow/codec.h driver/include/aicaflow/protocol.h
	mkdir -p build
	clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Idriver/include -Itools/author tools/author/afx_compile_c.c tools/author/afx_midi_c.c tools/author/afx_sample_c.c tools/author/afx_ya2beam.c tools/test/test_afx_compile_c.c driver/common/codec.c -lm -o $@

$(DEMO_ASSETS): tools/author/afx_demo_assets.c tools/author/afx_compile_c.c tools/author/afx_compile_c.h driver/common/codec.c driver/include/aicaflow/codec.h driver/include/aicaflow/protocol.h
	mkdir -p build
	clang -std=c11 -O2 -Wall -Wextra -Werror -Idriver/include tools/author/afx_demo_assets.c tools/author/afx_compile_c.c driver/common/codec.c -lm -o $@

$(BANK_COMPILER): tools/author/afx_bank_c.c tools/author/afx_compile_c.c tools/author/afx_compile_c.h tools/author/afx_midi_c.c tools/author/afx_midi_c.h tools/author/afx_sample_c.c tools/author/afx_sample_c.h tools/author/afx_sf2_c.c tools/author/afx_sf2_c.h tools/author/afx_ya2beam.c driver/common/codec.c driver/include/aicaflow/codec.h driver/include/aicaflow/protocol.h
	mkdir -p build
	clang -std=c11 -O2 -Wall -Wextra -Werror -Idriver/include tools/author/afx_bank_c.c tools/author/afx_compile_c.c tools/author/afx_midi_c.c tools/author/afx_sample_c.c tools/author/afx_sf2_c.c tools/author/afx_ya2beam.c driver/common/codec.c -lm -o $@

$(PROFILE_COMPILER): tools/author/afx_profile_c.c driver/common/codec.c driver/include/aicaflow/codec.h driver/include/aicaflow/protocol.h
	mkdir -p build
	clang -std=c11 -O2 -Wall -Wextra -Werror -Idriver/include tools/author/afx_profile_c.c driver/common/codec.c -o $@

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
	rm -f $(C_COMPILER) $(C_COMPILER_TEST) $(DEMO_ASSETS) $(BANK_COMPILER) $(PROFILE_COMPILER) \
		build/afx_compile_c build/test_afx_compile_c build/afx_bank_c build/afx_profile_c
