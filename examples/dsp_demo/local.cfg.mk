ENJ_BASENAME := aicaflow_dsp_demo
OPTLEVEL := 2
ENJ_CFLAGS += -Wall -Wextra -Werror
ENJ_INCLUDES += -I../../driver/include -I../../driver/sh4/include
ENJ_LDLIBS += ../../driver/sh4/libaicaflow_host.a

$(ENJ_BUILDDIR)/demo.mid: ../../tools/make_dsp_demo_midi.py
	@mkdir -p $(@D)
	python3 $< $@

$(ENJ_BUILDDIR)/demo.afb $(ENJ_BUILDDIR)/demo.afx &: $(ENJ_BUILDDIR)/demo.mid mapping.json ../../tools/afx_compile.py ../../tools/afx_music_bank.py ../../tools/afx_midi.py
	python3 ../../tools/afx_compile.py $< mapping.json $(ENJ_BUILDDIR)/demo.afx --bank $(ENJ_BUILDDIR)/demo.afb

$(ENJ_BUILDDIR)/code/main.o: $(ENJ_BUILDDIR)/demo.afb $(ENJ_BUILDDIR)/demo.afx ../../firmware/aicaflow.drv
$(ENJ_BINDIR)/$(ENJ_BASENAME).elf: ../../driver/sh4/libaicaflow_host.a

../../driver/sh4/libaicaflow_host.a:
	$(MAKE) -C $(@D)
