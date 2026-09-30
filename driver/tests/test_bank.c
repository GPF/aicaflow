#include <aicaflow/bank.h>
#include <aicaflow/codec.h>
#include <aicaflow/dsp.h>

#include <assert.h>
#include <string.h>

/* Supplies the simulated SPU transport used by the real allocator and loader. */
#include "test_transport.c"

static void firmware(uint8_t image[64]) {
    uint32_t manifest[] = {AFX_FIRMWARE_MAGIC, AFX_ABI_VERSION, AFX_LAYOUT_ID, 64, 64,
                           AFX_ASSET_MAX, AFX_PRIVATE_BASE + 64 * sizeof(afx_runtime_slot_t),
                           AFX_STACK_BASE};
    memset(image, 0, 64);
    for (unsigned i = 0; i < 8; ++i) afx_write32(image + 32 + 4 * i, manifest[i]);
}

static void bank_file(uint8_t data[96], uint32_t low, uint32_t high) {
    memset(data, 0, 96);
    afx_write32(data, AFX_BANK_MAGIC); afx_write32(data + 4, AFX_BANK_VERSION);
    afx_write32(data + 8, low); afx_write32(data + 12, high);
    afx_write32(data + 16, AFX_BANK_HEADER_BYTES); afx_write32(data + 20, 64);
    afx_write32(data + 24, 96);
}

static void flow_file(uint8_t data[160], uint32_t low, uint32_t high) {
    memset(data, 0, 160);
    afx_write32(data, AFX_FILE_MAGIC); afx_write32(data + 4, AFX_FILE_VERSION);
    afx_write32(data + 8, 160); afx_write32(data + 16, 96); afx_write32(data + 20, 64);
    afx_write32(data + 24, AFX_SETUP_BYTES); afx_write32(data + 28, 9);
    afx_write32(data + 32, 1); afx_write32(data + 36, 1);
    afx_write32(data + 40, low); afx_write32(data + 44, high);
    afx_write32(data + 48, 80); afx_write32(data + 52, 1);
    afx_write32(data + 64, 1); afx_write32(data + 68, 1000); afx_write32(data + 72, 1);
    afx_write32(data + 80, 0); afx_write32(data + 84, 0); afx_write32(data + 88, 64);
    afx_write16(data + 96 + 6, 31); /* A valid 32-frame PCM16 loop range. */
    data[96 + AFX_SETUP_BYTES] = AFX_OP_NOTE_PL;
    data[96 + AFX_SETUP_BYTES + 8] = AFX_OP_END;
}

int main(void) {
    uint8_t fw[64], bank_data[96], flow_data[160], seek_data[48];
    afx_bank_t bank = {0};
    afx_asset_t flow;
    firmware(fw); assert(afx_init(fw, sizeof(fw)) == AFX_OK);
    afx_dsp_program_t dsp;
    afx_mem_stats_t memory;
    assert(afx_mem_stats(&memory) == AFX_OK && memory.asset_limit == AFX_ASSET_MAX);
    assert(afx_dsp_program_gain(&dsp, 8192) == AFX_OK);
    sleep_observe_reference = AFX_DSP_SCENE_REFERENCE;
    assert(afx_dsp_scene_program(&dsp, sizeof(dsp)) == AFX_OK);
    assert(afx_mem_stats(&memory) == AFX_OK && memory.asset_limit == AFX_ASSET_MAX);
    sleep_observe_reference = AFX_DSP_SCENE_REFERENCE;
    assert(afx_dsp_scene_disable() == AFX_OK);
    assert(afx_dsp_program_delay(&dsp, 7938, 8192, false) == AFX_OK);
    sleep_observe_reference = AFX_DSP_SCENE_REFERENCE;
    assert(afx_dsp_scene_program_ring(&dsp, sizeof(dsp), 0) == AFX_OK);
    assert(afx_mem_stats(&memory) == AFX_OK && memory.asset_limit == AFX_CONTROL_BASE - AFX_DSP_MIN_BYTES);
    assert(afx_dsp_program_delay(&dsp, 9000, 8192, false) == AFX_OK);
    assert(afx_dsp_scene_program_ring(&dsp, sizeof(dsp), 0) == -AFX_BAD_BOUNDS);
    sleep_observe_reference = AFX_DSP_SCENE_REFERENCE;
    assert(afx_dsp_scene_disable() == AFX_OK);
    assert(afx_mem_stats(&memory) == AFX_OK && memory.asset_limit == AFX_ASSET_MAX);
    bank_file(bank_data, 0x12345678u, 0x9abcdef0u);
    flow_file(flow_data, 0x12345678u, 0x9abcdef0u);
    assert(afx_file_validate(flow_data, sizeof(flow_data), NULL) == AFX_OK);
    afx_write32(flow_data + 4, AFX_ABI_VERSION);
    assert(afx_file_validate(flow_data, sizeof(flow_data), NULL) == AFX_BAD_FORMAT);
    afx_write32(flow_data + 4, AFX_FILE_VERSION);
    assert(afx_bank_load_memory(&bank, bank_data, sizeof(bank_data)) == AFX_OK);
    assert(afx_bank_flow_upload(&bank, flow_data, sizeof(flow_data), &flow) == AFX_OK);
    memset(seek_data, 0, sizeof(seek_data));
    afx_write32(seek_data, AFX_SEEK_MAGIC); afx_write32(seek_data + 4, AFX_SEEK_VERSION);
    afx_write32(seek_data + 8, 1); afx_write32(seek_data + 12, 0x12345678u);
    afx_write32(seek_data + 16, 0x9abcdef0u); afx_write32(seek_data + 20, AFX_SEEK_HEADER_BYTES);
    afx_write32(seek_data + 24, 16); afx_write32(seek_data + 28, sizeof(seek_data));
    afx_write32(seek_data + 32, AFX_CHECKPOINT_MAGIC);
    afx_write32(seek_data + 36, AFX_CHECKPOINT_VERSION);
    assert(afx_flow_seek_index_load_memory(flow, seek_data, sizeof(seek_data)) == AFX_OK);
    afx_write32(seek_data + 8, 2);
    assert(afx_flow_seek_index_load_memory(flow, seek_data, sizeof(seek_data)) == -AFX_BAD_SAMPLE);
    assert(afx_bank_release(&bank) == -AFX_ASSET_REFERENCED);
    assert(afx_asset_free(flow) == AFX_OK);
    assert(afx_bank_release(&bank) == AFX_OK);
    afx_shutdown();
    return 0;
}
