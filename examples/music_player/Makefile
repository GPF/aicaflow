include ../../third_party/enDjinn/base_link.mk

SOURCES := sources/chopin-op28-no20.mid sources/bach-bwv1007-prelude.mid \
	sources/grieg-mountain-king.mid
DEFAULT_SOUNDFONT := sources/GeneralUser.sf2
SOUNDFONT ?= $(DEFAULT_SOUNDFONT)
ASSET_DIR := build/assets
ASSET_STAMP := $(ASSET_DIR)/.prepared
ASSETS := $(addprefix $(ASSET_DIR)/,classical.afb chopin-op28-no20.afx chopin-op28-no20.afc chopin-op28-no20.afv \
	bach-bwv1007-prelude.afx bach-bwv1007-prelude.afc bach-bwv1007-prelude.afv \
	grieg-mountain-king.afx grieg-mountain-king.afc grieg-mountain-king.afv)
DISC := $(ENJ_ROMDIR)/$(ENJ_BASENAME)
DISC_STAMP := $(DISC)/.assets
FIRMWARE := $(DISC)/aicaflow.drv
AUTHOR := ../../build/afx_compile_c
BANK_AUTHOR := ../../build/afx_bank_c

assets: $(ASSET_STAMP) $(FIRMWARE)

fetch-assets:
	sh fetch_assets.sh sources

$(SOURCES) $(DEFAULT_SOUNDFONT): fetch_assets.sh
	sh fetch_assets.sh sources

ifneq ($(SOUNDFONT),$(DEFAULT_SOUNDFONT))
$(SOUNDFONT):
	@test -f "$@" || { echo "Missing alternate SoundFont: $@" >&2; exit 1; }
endif

$(AUTHOR) $(BANK_AUTHOR): ../../tools/author/afx_compile_c.c ../../tools/author/afx_compile_c.h \
	../../tools/author/afx_compile_c_cli.c ../../tools/author/afx_midi_c.c \
	../../tools/author/afx_midi_c.h ../../tools/author/afx_sf2_c.c ../../tools/author/afx_sf2_c.h \
	../../tools/author/afx_bank_c.c
	$(MAKE) -C ../.. compiler

$(ASSET_STAMP): classical.afbm $(SOURCES) $(SOUNDFONT) $(BANK_AUTHOR) ../../tools/author/afx_sample_c.c ../../tools/author/afx_ya2beam.c
	@mkdir -p $(ASSET_DIR) build/include
	$(BANK_AUTHOR) classical.afbm "$(ASSET_DIR)" "$(ASSET_DIR)/classical.afb"
	@set -e; { \
		printf '%s\n' 'static const struct song { const char *title, *file, *bank, *dsp; bool wraps; uint32_t bytes, sample_bytes, stream_bytes, setup_bytes, padding_bytes, command_baseline_bytes, note_count; uint16_t sample_count, setup_count; uint8_t channel_count; } songs[] = {'; \
		for entry in 'chopin-op28-no20|Chopin - Prelude Op. 28 No. 20' 'bach-bwv1007-prelude|Bach - Cello Suite 1 Prelude' 'grieg-mountain-king|Grieg - Hall of the Mountain King'; do \
			name=$${entry%%|*}; title=$${entry#*|}; set -- $$($(AUTHOR) --summary "$(ASSET_DIR)/classical.afb" "$(ASSET_DIR)/$$name.afx"); \
			printf '{"%s","%s.afx",NULL,NULL,false,%s,%s,%s,%s,0,0,0,%s,%s,%s},\n' "$$title" "$$name" "$$1" "$$2" "$$3" "$$4" "$$5" "$$5" "$$6"; \
		done; \
		printf '%s\n' '};'; \
	} > build/include/songs.h
	touch $@

build/include/songs.h: $(ASSET_STAMP)

$(DISC_STAMP): $(ASSET_STAMP)
	@mkdir -p $(DISC)
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
	test -f $(ASSET_DIR)/classical.afb
	test $$(find $(ASSET_DIR) -name '*.afx' | wc -l) -eq 3
	test $$(find $(ASSET_DIR) -name '*.afc' | wc -l) -eq 3
	test $$(find $(ASSET_DIR) -name '*.afv' | wc -l) -eq 3
	@for flow in $(ASSET_DIR)/*.afx; do ../../driver/build/afx_validate "$$flow"; done
	@task_tmp=$$(mktemp -d); trap 'rm -rf "$$task_tmp"' EXIT; mkdir "$$task_tmp/out"; \
	$(BANK_AUTHOR) --create-map "$$task_tmp/generated.afbm" "$(SOUNDFONT)" generated "$(firstword $(SOURCES))" && \
	$(BANK_AUTHOR) "$$task_tmp/generated.afbm" "$$task_tmp/out" "$$task_tmp/generated.afb" && \
	test -s "$$task_tmp/generated.afb" -a -s "$$task_tmp/out/generated.afx"
