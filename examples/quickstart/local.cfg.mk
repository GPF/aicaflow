ENJ_BASENAME := aicaflow_quickstart
OPTLEVEL := 2
ENJ_CFLAGS += -Wall -Wextra -Werror
ENJ_INCLUDES += -I../../driver/include -I../../driver/sh4/include
ENJ_LDLIBS += ../../driver/sh4/libaicaflow_host.a

$(ENJ_BUILDDIR)/fixture.mid: ../../tools/make_fixture_midi.py
	@mkdir -p $(@D)
	python3 $< $@

$(ENJ_BUILDDIR)/fixture.afb $(ENJ_BUILDDIR)/fixture.afx &: $(ENJ_BUILDDIR)/fixture.mid mapping.json ../../tools/afx_compile.py ../../tools/afx_music_bank.py ../../tools/afx_midi.py
	python3 ../../tools/afx_compile.py $< mapping.json $(ENJ_BUILDDIR)/fixture.afx --bank $(ENJ_BUILDDIR)/fixture.afb

$(ENJ_BUILDDIR)/code/main.o: $(ENJ_BUILDDIR)/fixture.afb $(ENJ_BUILDDIR)/fixture.afx ../../firmware/aicaflow.drv
$(ENJ_BINDIR)/$(ENJ_BASENAME).elf: ../../driver/sh4/libaicaflow_host.a

../../driver/sh4/libaicaflow_host.a:
	$(MAKE) -C $(@D)
