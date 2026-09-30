#include <kos.h>

#include <aicaflow/dsp.h>
#include <aicaflow/bank.h>
#include <aicaflow/host.h>

#include <stdio.h>
#include <stdalign.h>

alignas(32) static const unsigned char firmware[] = {
#embed "../../../firmware/aicaflow.drv"
};
alignas(32) static const unsigned char fixture[] = {
#embed "../build/fixture.afx"
};
alignas(32) static const unsigned char fixture_bank[] = {
#embed "../build/fixture.afb"
};

static int wait_for(afx_instance_t instance, uint32_t state, uint32_t timeout_ms) {
    uint64_t deadline = timer_ms_gettime64() + timeout_ms;
    do {
        afx_instance_status_t status;
        if (afx_update() < 0 || afx_instance_status(instance, &status)) return 0;
        if (status.state == state) return 1;
        thd_sleep(1);
    } while (timer_ms_gettime64() < deadline);
    return 0;
}

static int wait_recycled(afx_instance_t instance, uint32_t timeout_ms) {
    uint64_t deadline = timer_ms_gettime64() + timeout_ms;
    do {
        if (afx_update() < 0) return 0;
        afx_instance_status_t status;
        if (afx_instance_status(instance, &status) == -AFX_STALE_GENERATION) return 1;
        thd_sleep(1);
    } while (timer_ms_gettime64() < deadline);
    return 0;
}

static int dsp_room_upload(void) {
    afx_dsp_program_t program;
    int result = afx_dsp_program_room(&program, 22936, 11464, false, 256, false);
    return result ? result : afx_dsp_scene_program(&program, sizeof(program));
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    afx_asset_t flow;
    afx_instance_t instance;
    afx_bank_t bank = {0};
    int result = afx_init(firmware, sizeof(firmware));
    if (!result) result = afx_bank_load_memory(&bank, fixture_bank, sizeof(fixture_bank));
    if (!result) result = afx_bank_flow_upload(&bank, fixture, sizeof(fixture), &flow);
    if (!result) result = afx_instance_activate(flow, &instance);
    if (!result && !wait_for(instance, AFX_RUNNING, 1000)) result = -1;
    if (!result) result = dsp_room_upload();
    if (!result && !wait_for(instance, AFX_DONE, 12000)) result = -1;
    if (!result) result = afx_instance_recycle(instance);
    if (!result && !wait_recycled(instance, 1000)) result = -1;
    if (!result) result = afx_asset_free(flow);
    if (!result) result = afx_bank_release(&bank);
    printf("Aicaflow quickstart: %s (%d)\n", result ? "FAIL" : "PASS", result);
    afx_shutdown();
    return result ? 1 : 0;
}
