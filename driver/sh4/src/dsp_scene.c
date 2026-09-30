#include "host_internal.h"

static uint16_t dsp_word(const uint8_t *program, uint32_t offset) {
    return program[offset] | (uint16_t)program[offset + 1u] << 8;
}
static uint32_t ring_bytes(uint8_t rbl) {
    return rbl == AFX_DSP_RING_NONE ? 0 : AFX_DSP_MIN_BYTES << rbl;
}
static uint32_t ring_limit(uint8_t rbl) {
    return AFX_ASSET_MAX - ring_bytes(rbl);
}
static int scene_command(uint32_t opcode, uint32_t flags) {
    uint32_t sequence = new_sequence();
    int result = enqueue(opcode, AFX_DSP_SCENE_REFERENCE, sequence, flags, NULL, 0);
    if (result) return result;
    for (unsigned waited = 0; waited < 2000; ++waited) {
        if (read_spu_word(AFX_STATUS_ADDR + offsetof(afx_status_t, dsp_sequence)) == sequence) {
            uint32_t status = read_spu_word(AFX_STATUS_ADDR + offsetof(afx_status_t, dsp_result));
            return status ? -(int)status : AFX_OK;
        }
        thd_sleep(1);
    }
    return -AFX_TIMEOUT;
}
static int dsp_scene_program(const void *data, uint32_t bytes, uint8_t rbl) {
    HOST_GUARD(-AFX_BUSY);
    const uint8_t *program = data;
    if (!program || bytes != AFX_DSP_PROGRAM_BYTES || rbl > 3) return -AFX_BAD_COMMAND;
    int memory_format = -1;
    for (uint32_t step = 0; step < 128; ++step) {
        uint16_t w2 = dsp_word(program, step * 8u + 4u);
        if ((w2 & 0x8000u) || (!(step & 1u) && (w2 & 0x6000u)) ||
            ((w2 & 0x1000u) && ((w2 >> 8) & 15u) > 1u)) return -AFX_BAD_COMMAND;
        if (w2 & 0x6000u) {
            int nofl = (dsp_word(program, step * 8u + 6u) >> 15) & 1u;
            if (memory_format >= 0 && memory_format != nofl) return -AFX_BAD_COMMAND;
            memory_format = nofl;
        }
    }
    if ((dsp_word(program, 1408) | dsp_word(program, 1410)) & ~0x0f1fu)
        return -AFX_BAD_COMMAND;
    if (memory_format < 0) rbl = AFX_DSP_RING_NONE;
    uint32_t bytes_reserved = ring_bytes(rbl);
    if (memory_format >= 0) {
        uint32_t words = bytes_reserved / 2u;
        for (uint32_t address = 0; address < AFX_DSP_ADDRESSES; ++address)
            if (dsp_word(program, 1024u + AFX_DSP_COEFFICIENTS * 2u + address * 2u) >= words)
                return -AFX_BAD_BOUNDS;
    }
    uint32_t previous_limit = g_asset_limit;
    if (!allocator_set_limit(ring_limit(rbl))) return -AFX_NO_AICA_RAM;
    /* ARM7 clears its DSP ownership and installs safe NOPs before every
       program, including a replacement of an active scene. */
    /* Preserve the established flags=1 wire form for the normal 64 Kiword
       scene. Smaller rings carry their RBL+1 in bits 8..9; bit 10 means
       the DSP program uses no delay ring. */
    uint32_t flags = rbl == AFX_DSP_RING_NONE ? 0x401u :
                     rbl == 3 ? 1u : 1u | ((uint32_t)(rbl + 1u) << 8);
    int result = scene_command(AFX_CMD_DSP_ENABLE, flags);
    if (result) { (void)allocator_set_limit(previous_limit); return result; }
    g_dsp_scene = true;
    g2_write_32(0xa0702000u, 0);
    g2_write_32(0xa0702004u, 0);
    /* Remove all memory/output writes before replacing their operands. */
    for (uint32_t i = 0; i < 128; ++i) g2_write_32(0xa0703408u + i * 16u, 2);
    thd_sleep(2);
    if (memory_format == 0)
        for (uint32_t i = 0; i < bytes_reserved; i += 4)
            g2_write_32(g_spu_base + ring_limit(rbl) + i, 0x60006000u);
    for (uint32_t i = 0; i < 128; ++i)
        g2_write_32(0xa0703000u + i * 4u, dsp_word(program, 1024u + i * 2u));
    for (uint32_t i = 0; i < 64; ++i)
        g2_write_32(0xa0703200u + i * 4u, dsp_word(program, 1280u + i * 2u));
    for (uint32_t i = 0; i < 512; ++i)
        g2_write_32(0xa0703400u + i * 4u, dsp_word(program, i * 2u));
    for (uint32_t i = 0; i < 512; ++i)
        if ((g2_read_32(0xa0703400u + i * 4u) & 0xffffu) != dsp_word(program, i * 2u))
            return -AFX_BAD_COMMAND;
    thd_sleep(2);
    g_dsp_return_left = dsp_word(program, 1408);
    g_dsp_return_right = dsp_word(program, 1410);
    g2_write_32(0xa0702000u, g_dsp_return_left);
    g2_write_32(0xa0702004u, g_dsp_return_right);
    return AFX_OK;
}
int afx_dsp_scene_program(const void *data, uint32_t bytes) {
    return dsp_scene_program(data, bytes, 3);
}
int afx_dsp_scene_program_ring(const void *data, uint32_t bytes, uint8_t rbl) {
    return dsp_scene_program(data, bytes, rbl);
}
int afx_dsp_scene_returns(bool enabled) {
    HOST_GUARD(-AFX_BUSY);
    if (!g_dsp_scene) return -AFX_BUSY;
    g2_write_32(0xa0702000u, enabled ? g_dsp_return_left : 0);
    g2_write_32(0xa0702004u, enabled ? g_dsp_return_right : 0);
    return AFX_OK;
}
int afx_dsp_scene_disable(void) {
    HOST_GUARD(-AFX_BUSY);
    if (!g_dsp_scene) return -AFX_BUSY;
    int result = scene_command(AFX_CMD_DSP_DISABLE, 1);
    if (!result) {
        g_dsp_scene = false;
        g_dsp_return_left = g_dsp_return_right = 0;
        if (!allocator_set_limit(AFX_ASSET_MAX)) return -AFX_NO_HOST_RAM;
    }
    return result;
}
