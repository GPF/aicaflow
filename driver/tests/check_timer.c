#include <kos.h>
#include <aicaflow/host.h>
#include <stdio.h>

static const uint8_t firmware[] = {
#embed "../../firmware/aicaflow.drv"
};

int main(void) {
    int result = afx_init(firmware, sizeof(firmware));
    if (result) {
        printf("AFX_TIMER FAIL init=%d\n", result);
        return 1;
    }
    /* Use SH-4 time so a broken ARM FIQ cannot hang the check itself. */
    for (unsigned i = 0; i < 3; ++i) {
        uint64_t start_ms = timer_ms_gettime64();
        uint32_t start_tick = afx_status_timer_ticks();
        uint32_t heartbeat = afx_status_heartbeat();
        thd_sleep(1000);
        uint32_t ticks = afx_status_timer_ticks() - start_tick;
        uint32_t beats = afx_status_heartbeat() - heartbeat;
        printf("AFX_TIMER elapsed_ms=%llu ticks=%lu heartbeat=%lu\n",
               (unsigned long long)(timer_ms_gettime64() - start_ms),
               (unsigned long)ticks, (unsigned long)beats);
        if (!ticks || !beats) { result = 1; break; }
    }
    afx_shutdown();
    puts(result ? "AFX_TIMER FAIL" : "AFX_TIMER PASS");
    return result;
}
