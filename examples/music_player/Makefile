include ../../third_party/enDjinn/base_link.mk

SOURCES := sources/chopin-op28-no20.mid sources/bach-bwv1007-prelude.mid \
	sources/grieg-mountain-king.mid
DEFAULT_SOUNDFONT := sources/GeneralUser.sf2
SOUNDFONT ?= $(DEFAULT_SOUNDFONT)
ASSET_DIR := build/assets
ASSET_STAMP := $(ASSET_DIR)/.prepared
ASSETS := $(addprefix $(ASSET_DIR)/,chopin-op28-no20.afb chopin-op28-no20.afx chopin-op28-no20.afc chopin-op28-no20.afv \
	bach-bwv1007-prelude.afb bach-bwv1007-prelude.afx bach-bwv1007-prelude.afc bach-bwv1007-prelude.afv \
	grieg-mountain-king.afb grieg-mountain-king.afx grieg-mountain-king.afc grieg-mountain-king.afv)
DISC := $(ENJ_ROMDIR)/$(ENJ_BASENAME)
DISC_STAMP := $(DISC)/.assets
FIRMWARE := $(DISC)/aicaflow.drv

assets: $(ASSET_STAMP) $(FIRMWARE)

fetch-assets:
	python3 fetch_assets.py sources

$(SOURCES) $(DEFAULT_SOUNDFONT):
	python3 fetch_assets.py sources

ifneq ($(SOUNDFONT),$(DEFAULT_SOUNDFONT))
$(SOUNDFONT):
	@test -f "$@" || { echo "Missing alternate SoundFont: $@" >&2; exit 1; }
endif

$(ASSET_STAMP): prepare.py $(SOURCES) $(SOUNDFONT) ../../tools/afx_compile.py ../../tools/afx_midi.py ../../tools/afx_sf2.py ../../tools/afx_visualize.py ../../tools/afx_metadata.py
	python3 prepare.py --soundfont "$(SOUNDFONT)" --output "$(ASSET_DIR)"
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

.PHONY: assets fetch-assets check

check: $(ENJ_BINDIR)/$(ENJ_BASENAME).elf $(DISC_STAMP)
	test $$(find $(ASSET_DIR) -name '*.afb' | wc -l) -eq 3
	test $$(find $(ASSET_DIR) -name '*.afx' | wc -l) -eq 3
	test $$(find $(ASSET_DIR) -name '*.afc' | wc -l) -eq 3
	test $$(find $(ASSET_DIR) -name '*.afv' | wc -l) -eq 3
