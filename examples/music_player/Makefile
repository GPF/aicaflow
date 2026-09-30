include ../../third_party/enDjinn/base_link.mk

SOURCES := sources/chopin-op28-no20.mid sources/bach-bwv1007-prelude.mid \
	sources/grieg-mountain-king.mid
DEFAULT_SOUNDFONT := sources/GeneralUser.sf2
PIANO_SOUNDFONT ?= $(DEFAULT_SOUNDFONT)
CELLO_SOUNDFONT ?= $(DEFAULT_SOUNDFONT)
PIANO_FORMAT ?= pcm16
CELLO_FORMAT ?= pcm16
PIANO_CHANNEL ?= stereo
CELLO_CHANNEL ?= stereo
SOUNDFONTS := $(sort $(PIANO_SOUNDFONT) $(CELLO_SOUNDFONT))
NONDEFAULT_SOUNDFONTS := $(filter-out $(DEFAULT_SOUNDFONT),$(SOUNDFONTS))
ASSET_DIR := build/assets
BASE_DIR := build/base
MAP := build/classical.afbm
ASSET_STAMP := $(ASSET_DIR)/.prepared
PROFILES := $(addprefix profiles/,chopin-op28-no20.afp bach-bwv1007-prelude.afp grieg-mountain-king.afp)
ASSETS := $(addprefix $(ASSET_DIR)/,chopin-op28-no20.afb chopin-op28-no20.afx chopin-op28-no20.afc chopin-op28-no20.afv \
	chopin-op28-no20.afi chopin-op28-no20.names.afi \
	bach-bwv1007-prelude.afb bach-bwv1007-prelude.afx bach-bwv1007-prelude.afc bach-bwv1007-prelude.afv \
	bach-bwv1007-prelude.afi bach-bwv1007-prelude.names.afi \
	grieg-mountain-king.afb grieg-mountain-king.afx grieg-mountain-king.afc grieg-mountain-king.afv \
	grieg-mountain-king.afi grieg-mountain-king.names.afi)
DISC := $(ENJ_ROMDIR)/$(ENJ_BASENAME)
DISC_STAMP := $(DISC)/.assets
FIRMWARE := $(DISC)/aicaflow.drv
AUTHOR := ../../build/afx_compile
BANK_AUTHOR := ../../build/afx_bank
PROFILE_AUTHOR := ../../build/afx_profile

assets: $(ASSET_STAMP) $(FIRMWARE)

fetch-assets:
	sh fetch_assets.sh sources

$(SOURCES) $(DEFAULT_SOUNDFONT): fetch_assets.sh
	sh fetch_assets.sh sources

$(NONDEFAULT_SOUNDFONTS):
	@test -f "$@" || { echo "Missing alternate SoundFont: $@" >&2; exit 1; }

$(AUTHOR) $(BANK_AUTHOR) $(PROFILE_AUTHOR): ../../tools/author/afx_compile_c.c ../../tools/author/afx_compile_c.h \
	../../tools/author/afx_compile_c_cli.c ../../tools/author/afx_midi_c.c \
	../../tools/author/afx_midi_c.h ../../tools/author/afx_sf2_c.c ../../tools/author/afx_sf2_c.h \
	../../tools/author/afx_bank_c.c ../../tools/author/afx_profile_c.c
	$(MAKE) -C ../.. compiler

$(MAP): classical.afbm $(SOUNDFONTS)
	@mkdir -p $(@D)
	@sed -e 's|@PIANO_SOUNDFONT@|$(abspath $(PIANO_SOUNDFONT))|g' \
		-e 's|@CELLO_SOUNDFONT@|$(abspath $(CELLO_SOUNDFONT))|g' \
		-e 's|@PIANO_FORMAT@|$(PIANO_FORMAT)|g' \
		-e 's|@CELLO_FORMAT@|$(CELLO_FORMAT)|g' \
		-e 's|@PIANO_CHANNEL@|$(PIANO_CHANNEL)|g' \
		-e 's|@CELLO_CHANNEL@|$(CELLO_CHANNEL)|g' "$<" > "$@"

$(ASSET_STAMP): Makefile $(MAP) $(PROFILES) $(SOURCES) $(SOUNDFONTS) $(BANK_AUTHOR) $(PROFILE_AUTHOR) ../../tools/author/afx_sample_c.c ../../tools/author/afx_ya2beam.c
	@mkdir -p $(ASSET_DIR) $(BASE_DIR) build/include
	$(BANK_AUTHOR) --per-song "$(MAP)" "$(BASE_DIR)"
	@set -e; for name in chopin-op28-no20 bach-bwv1007-prelude grieg-mountain-king; do \
		if [ "$(abspath $(PIANO_SOUNDFONT))" = "$(abspath $(DEFAULT_SOUNDFONT))" ] && [ "$(abspath $(CELLO_SOUNDFONT))" = "$(abspath $(DEFAULT_SOUNDFONT))" ]; then \
			$(PROFILE_AUTHOR) apply "$(BASE_DIR)/$$name.afx" "$(BASE_DIR)/$$name.afc" "profiles/$$name.afp" "$(ASSET_DIR)/$$name.afx" "$(ASSET_DIR)/$$name.afc"; \
		else cp "$(BASE_DIR)/$$name.afx" "$(BASE_DIR)/$$name.afc" "$(ASSET_DIR)/"; fi; \
		cp "$(BASE_DIR)/$$name.afb" "$(BASE_DIR)/$$name.afv" "$(BASE_DIR)/$$name.afi" "$(BASE_DIR)/$$name.names.afi" "$(ASSET_DIR)/"; \
	done
	@set -e; { \
		printf '%s\n' 'static const struct song { const char *title, *file, *bank, *dsp; bool wraps; uint32_t bytes, sample_bytes, stream_bytes, setup_bytes, padding_bytes, command_baseline_bytes, note_count; uint16_t sample_count, setup_count, tempo; uint8_t channel_count; } songs[] = {'; \
		for entry in 'chopin-op28-no20|Chopin - Prelude Op. 28 No. 20' 'bach-bwv1007-prelude|Bach - Cello Suite 1 Prelude' 'grieg-mountain-king|Grieg - Hall of the Mountain King'; do \
			name=$${entry%%|*}; title=$${entry#*|}; set -- $$($(AUTHOR) --summary "$(ASSET_DIR)/$$name.afb" "$(ASSET_DIR)/$$name.afx"); \
			bytes=$$1; samples=$$2; stream=$$3; setup=$$4; notes=$$5; setups=$$6; channels=$$7; \
			if [ "$(abspath $(PIANO_SOUNDFONT))" = "$(abspath $(DEFAULT_SOUNDFONT))" ] && [ "$(abspath $(CELLO_SOUNDFONT))" = "$(abspath $(DEFAULT_SOUNDFONT))" ]; then set -- $$($(PROFILE_AUTHOR) describe "$(BASE_DIR)/$$name.afx" "profiles/$$name.afp"); else set -- dry 0 256; fi; dsp=$$1; tempo=$$3; \
			if [ "$$dsp" = dry ]; then dsp=NULL; else dsp="\"$$dsp\""; fi; \
			printf '{"%s","%s.afx","%s.afb",%s,false,%s,%s,%s,%s,0,0,%s,%s,%s,%s,%s},\n' "$$title" "$$name" "$$name" "$$dsp" "$$bytes" "$$samples" "$$stream" "$$setup" "$$notes" "$$setups" "$$setups" "$$tempo" "$$channels"; \
		done; \
		printf '%s\n' '};'; \
	} > build/include/songs.h
	touch $@

build/include/songs.h: $(ASSET_STAMP)

$(DISC_STAMP): $(ASSET_STAMP)
	@mkdir -p $(DISC)
	@rm -f "$(DISC)/classical.afb"
	cp $(ASSETS) $(DISC)/
	touch $@

$(FIRMWARE): ../../firmware/aicaflow.drv
	@mkdir -p $(@D)
	cp $< $@

$(ENJ_BUILDDIR)/code/main.o: build/include/songs.h player.c $(ASSET_STAMP) ../../firmware/aicaflow.drv
$(ENJ_BINDIR)/$(ENJ_BASENAME).elf: $(DISC_STAMP) $(FIRMWARE) ../../driver/sh4/libaicaflow_host.a
$(ENJ_BINDIR)/$(ENJ_BASENAME).cdi: $(ENJ_BINDIR)/$(ENJ_BASENAME).elf $(DISC_STAMP) $(FIRMWARE)

../../driver/sh4/libaicaflow_host.a:
	$(MAKE) -C $(@D)

../../driver/build/afx_validate:
	$(MAKE) -C ../../driver smoke

.PHONY: assets fetch-assets check

check: $(ENJ_BINDIR)/$(ENJ_BASENAME).elf $(DISC_STAMP) ../../driver/build/afx_validate
	@for name in chopin-op28-no20 bach-bwv1007-prelude grieg-mountain-king; do \
		for extension in afb afx afc afv afi names.afi; do test -s "$(ASSET_DIR)/$$name.$$extension"; done; \
	done
	@for flow in $(ASSET_DIR)/*.afx; do ../../driver/build/afx_validate "$$flow"; done
	@task_tmp=$$(mktemp -d); trap 'rm -rf "$$task_tmp"' EXIT; mkdir "$$task_tmp/out"; \
	$(BANK_AUTHOR) --create-map "$$task_tmp/generated.afbm" "$(PIANO_SOUNDFONT)" generated "$(firstword $(SOURCES))" && \
	$(BANK_AUTHOR) "$$task_tmp/generated.afbm" "$$task_tmp/out" "$$task_tmp/generated.afb" && \
	test -s "$$task_tmp/generated.afb" -a -s "$$task_tmp/out/generated.afx"
