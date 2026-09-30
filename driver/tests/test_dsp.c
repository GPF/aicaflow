#include <aicaflow/dsp.h>
#include <assert.h>
#include <stdint.h>

static void valid(const afx_dsp_program_t *program) {
    for (uint32_t step = 0; step < AFX_DSP_STEPS; ++step) {
        uint16_t word = program->words[step * 4 + 2];
        assert(!(word & 0x8000u));
        assert((step & 1u) || !(word & 0x6000u));
        assert(!(word & 0x1000u) || ((word >> 8) & 15u) <= 1u);
    }
}

int main(void) {
    static const char *names[] = {
        "room_large", "eq_presence", "resonant_filter", "phaser", "autopan", "pingpong",
        "multitap", "distortion", "overdrive", "resonators", "bow_texture", "pitch_shift",
        "harmonizer", "gain", "invert", "delay", "echo", "lowpass", "highpass", "diffuser",
        "room", "room_warm", "ringmod", "tremolo", "chorus", "flanger",
    };
    afx_dsp_program_t program;
    for (uint32_t i = 0; i < sizeof(names) / sizeof(*names); ++i) {
        assert(afx_dsp_program_preset(&program, names[i]) == AFX_OK);
        assert(afx_dsp_program_preset_description(names[i]));
        valid(&program);
        assert(afx_dsp_program_demo(&program, names[i]) == AFX_OK);
        valid(&program);
    }
    assert(afx_dsp_program_demo(&program, "echo") == AFX_OK);
    assert(program.words[AFX_DSP_MPRO_WORDS + 7] == 24576);
    assert(afx_dsp_program_demo(&program, "room") == AFX_OK);
    valid(&program);
    assert(afx_dsp_program_preset(&program, "missing") == -AFX_BAD_COMMAND);
    assert(afx_dsp_program_delay(&program, 0, 0, true) == -AFX_BAD_COMMAND);
    assert(afx_dsp_program_gain(&program, 1) == -AFX_BAD_COMMAND);
    /* AICA addresses MADRS through MASA << 1; odd slots are not reachable. */
    assert(afx_dsp_program_address(&program, 1, 0) == -AFX_BAD_COMMAND);
    assert(afx_dsp_program_room(&program, 22936, 11464, false, 256, false) == AFX_OK);
    assert(afx_dsp_program_room(&program, 20480, 12288, false, 128, false) == AFX_OK);
    valid(&program);
    static const uint16_t room_addresses[] = {
        149, 0, 8403, 8192, 17537, 16384, 26005, 24576,
        34545, 32768, 43049, 40960,
    };
    for (uint32_t i = 0; i < sizeof(room_addresses) / sizeof(*room_addresses); ++i)
        assert(program.words[AFX_DSP_MPRO_WORDS + AFX_DSP_COEFFICIENTS + i * 2] == room_addresses[i]);
    assert(program.words[AFX_DSP_PROGRAM_WORDS - 2] == 0x0f1f);
    assert(program.words[AFX_DSP_PROGRAM_WORDS - 1] == 0x0f0f);
    return 0;
}
