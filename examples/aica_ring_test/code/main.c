/*
 * aica_ring_test — mono PCM16 ring experiment, phase 1: upload bandwidth sweep.
 *
 * One looping AICAflow voice plays a 4096-frame ring (+4 guard frames).  While it
 * plays, the SH-4 rewrites the start of the ring with afx_mem_upload() (which is
 * upload_words() -> KOS spu_memload(): a CPU copy over G2, not DMA) using the
 * *same* sine data, so the audio stays a clean steady tone.  The SH-4 measures
 * upload time per call, throughput, back-to-back burst throughput, and whether
 * the playback cursor keeps pace with wall time while G2 is busy.
 *
 * NOT in this phase: the refill loop, underrun detection, stereo, any new API.
 * Bandwidth alone does not establish streaming viability.
 *
 * Cursor read: 32-bit RMW of 0xa070280c + 50 us settle under g2_lock(); see
 * docs/results/aica-cursor-probe.md.  Physical channel: AFX channel-map arena 0,
 * entry 0 (fresh AFX, one voice), also from the probe.
 */

#include <kos.h>

#include <aicaflow/bank.h>
#include <aicaflow/host.h>
#include <aicaflow/codec.h>

#include <math.h>
#include <stdio.h>
#include <stdalign.h>
#include <string.h>

#define AICA_REG_SH4   0xa0700000u
#define MON_SEL_WORD   (AICA_REG_SH4 + 0x280cu)
#define MON_POS_ADDR   (AICA_REG_SH4 + 0x2814u)
#define SPU_RAM_SH4    0xa0800000u

enum {
    RING_FRAMES = 4096, GUARD_FRAMES = 4,
    RING_BYTES = (RING_FRAMES + GUARD_FRAMES) * 2,
    BANK_BYTES = (RING_BYTES + 31) & ~31,
    BANK_ID_LOW = 0x474e4952u,          /* "RING" */
    SINE_CYCLES = 40,                   /* 40 * 44100 / 4096 = 430.7 Hz, integer cycles */
    PITCH_NATIVE = 0x0000,
    MIX_PLAY = 0x1424,
    ISO_REPS = 24, BURST_COUNT = 16, BURST_REPS = 4,
    SELECT_SETTLE_US = 50
};

alignas(32) static uint8_t bank_file[AFX_BANK_HEADER_BYTES + BANK_BYTES];
alignas(32) static uint8_t ring_pcm[RING_BYTES];    /* master copy, also the upload source */

static void make_ring(void) {
    for (int i = 0; i < RING_FRAMES + GUARD_FRAMES; i++) {
        int16_t s = (int16_t)(12000.0f * sinf(6.2831853f * SINE_CYCLES * (float)(i % RING_FRAMES) / RING_FRAMES));
        ring_pcm[i * 2] = (uint8_t)(s & 0xff);
        ring_pcm[i * 2 + 1] = (uint8_t)((uint16_t)s >> 8);
    }
}

static int load_bank(afx_bank_t *bank) {
    memset(bank_file, 0, sizeof(bank_file));
    afx_write32(bank_file, AFX_BANK_MAGIC); afx_write32(bank_file + 4, AFX_BANK_VERSION);
    afx_write32(bank_file + 8, BANK_ID_LOW); afx_write32(bank_file + 12, 0);
    afx_write32(bank_file + 16, AFX_BANK_HEADER_BYTES); afx_write32(bank_file + 20, BANK_BYTES);
    afx_write32(bank_file + 24, sizeof(bank_file));
    memcpy(bank_file + AFX_BANK_HEADER_BYTES, ring_pcm, RING_BYTES);
    return afx_bank_load_memory(bank, bank_file, sizeof(bank_file));
}

static int wait_for(afx_instance_t instance, uint32_t wanted, uint32_t timeout_ms) {
    uint64_t deadline = timer_ms_gettime64() + timeout_ms;
    do {
        afx_instance_status_t status;
        if (afx_update() < 0 || afx_instance_status(instance, &status)) return -1;
        if (status.state == wanted) return 0;
        thd_sleep(1);
    } while (timer_ms_gettime64() < deadline);
    return -1;
}

static int build_flow(const afx_bank_t *bank, afx_asset_t *out_flow) {
    uint16_t fields[AFX_FIELD_COUNT] = {0};
    fields[AFX_FIELD_CONTROL] = 0x0200u;
    fields[AFX_FIELD_SAMPLE_LOW] = 0;
    fields[AFX_FIELD_LOOP_END] = RING_FRAMES - 1;
    fields[AFX_FIELD_ENV_AD] = 0x001f;
    fields[AFX_FIELD_ENV_DR] = 0x001f;
    fields[AFX_FIELD_DIRECT] = 0x0f00u | 15u;
    fields[AFX_FIELD_MIX] = 0x0024;
    for (unsigned f = AFX_FIELD_FILTER_LEVEL0; f <= AFX_FIELD_FILTER_LEVEL4; ++f)
        fields[f] = 0x1fff;

    uint8_t stream[9];
    uint32_t bytes;
    uint16_t note[] = {PITCH_NATIVE, MIX_PLAY};
    afx_event_t event = {.opcode = AFX_OP_NOTE_PL, .channel = 0, .setup = 0,
                         .mask = AFX_NOTE_PL_MASK};
    if (afx_encode_event(stream, sizeof(stream), &event, note, &bytes)) return -1;
    uint32_t written = bytes;
    stream[written++] = AFX_OP_PARK;

    uint8_t file[96 + AFX_SETUP_BYTES + sizeof(stream)] = {0};
    uint32_t image_bytes = AFX_SETUP_BYTES + written;
    afx_write32(file, AFX_FILE_MAGIC); afx_write32(file + 4, AFX_FILE_VERSION);
    afx_write32(file + 8, 96 + image_bytes); afx_write32(file + 12, AFX_FLAG_CONTROLLED);
    afx_write32(file + 16, 96); afx_write32(file + 20, image_bytes);
    afx_write32(file + 24, AFX_SETUP_BYTES); afx_write32(file + 28, written);
    afx_write32(file + 36, 1); afx_write32(file + 40, bank->id.low); afx_write32(file + 44, bank->id.high);
    afx_write32(file + 48, 80); afx_write32(file + 52, 1);
    afx_write32(file + 64, 1); afx_write32(file + 68, 1000); afx_write32(file + 72, 1);
    afx_write32(file + 80, 0); afx_write32(file + 84, 0); afx_write32(file + 88, RING_BYTES);
    for (unsigned f = 0; f < AFX_FIELD_COUNT; ++f)
        afx_write16(file + 96 + f * 2u, fields[f]);
    memcpy(file + 96 + AFX_SETUP_BYTES, stream, written);
    afx_write32(file + 32, afx_control_id(file + 96, image_bytes));
    return afx_bank_flow_upload(bank, file, 96 + image_bytes, out_flow);
}

static int release_voice(afx_instance_t inst, afx_asset_t f) {
    int r = 0;
    if (inst) {
        r = afx_instance_stop(inst);
        if (!r) r = wait_for(inst, AFX_DONE, 1000);
        if (!r) r = afx_instance_recycle(inst);
        if (!r) {
            uint64_t deadline = timer_ms_gettime64() + 1000;
            r = -1;
            while (timer_ms_gettime64() < deadline) {
                afx_instance_status_t st;
                if (afx_update() < 0) break;
                if (afx_instance_status(inst, &st) == -AFX_STALE_GENERATION) { r = 0; break; }
                thd_sleep(1);
            }
        }
    }
    if (!r && f) r = afx_asset_free(f);
    return r;
}

/* Cursor read: 32-bit RMW select of 0xa070280c, settle, read 0xa0700000+0x2814. */
static uint32_t cursor_read(unsigned channel) {
    g2_ctx_t ctx = g2_lock();
    uint32_t w = g2_read_32(MON_SEL_WORD);
    g2_write_32(MON_SEL_WORD, (w & ~0xff00u) | ((uint32_t)channel << 8));
    timer_spin_delay_us(SELECT_SETTLE_US);
    uint32_t pos = g2_read_32(MON_POS_ADDR) & 0xffffu;
    g2_unlock(ctx);
    return pos;
}

/* Cursor plus a timestamp at the middle of the read (+-~50 us, ~+-2 samples). */
static uint32_t cursor_at(unsigned ch, uint64_t *t_mid) {
    uint64_t a = timer_us_gettime64();
    uint32_t pos = cursor_read(ch);
    uint64_t b = timer_us_gettime64();
    *t_mid = (a + b) / 2;
    return pos;
}

/* Signed deviation (samples) of the measured cursor advance from 44.1 samples/ms.
 * Returns false (and leaves *dev untouched) if the interval could be a whole loop. */
static bool cursor_dev(uint32_t p0, uint64_t t0, uint32_t p1, uint64_t t1,
                       uint32_t *advanced, double *expected, double *dev) {
    uint64_t dt = t1 - t0;
    *advanced = p1 >= p0 ? p1 - p0 : p1 + RING_FRAMES - p0;
    *expected = (double)dt * 0.0441;
    if (dt > 80000u) return false;
    double d = (double)*advanced - *expected;
    if (d > RING_FRAMES / 2) d -= RING_FRAMES;
    if (d < -RING_FRAMES / 2) d += RING_FRAMES;
    *dev = d;
    return true;
}

static bool verify_region(uint32_t spu_addr, uint32_t bytes) {
    g2_ctx_t ctx = g2_lock();
    bool ok = true;
    for (uint32_t i = 0; i < bytes && ok; i += 4) {
        uint32_t want; memcpy(&want, ring_pcm + i, 4);
        if (g2_read_32(SPU_RAM_SH4 + spu_addr + i) != want) ok = false;
    }
    g2_unlock(ctx);
    return ok;
}

#define CHECK(name, ok) do { printf("CHECK %s: %s\n", name, (ok) ? "PASS" : "FAIL"); \
                             if (!(ok)) all_ok = 0; } while (0)

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    alignas(32) static const unsigned char firmware[] = {
#embed "../../../firmware/aicaflow.drv"
    };

    afx_bank_t bank = {0};
    afx_asset_t flow = 0;
    afx_instance_t inst = 0;
    int result = 0, all_ok = 1;

    printf("AICA_RING_TEST_BEGIN phase=bandwidth_sweep\n");
    make_ring();
    result = afx_init(firmware, sizeof(firmware));
    if (!result) result = load_bank(&bank);
    if (!result) result = build_flow(&bank, &flow);
    if (!result) result = afx_instance_activate(flow, &inst);
    if (!result) result = wait_for(inst, AFX_PARKED, 1000);
    if (result) { printf("setup FAIL (%d)\n", result); goto done; }

    uint32_t base = afx_asset_addr(bank.asset);
    uint32_t chan_word;
    { g2_ctx_t c = g2_lock(); chan_word = g2_read_32(SPU_RAM_SH4 + AFX_CHANNEL_MAP_ARENA_ADDR); g2_unlock(c); }
    unsigned ch = chan_word;
    if (ch >= AFX_AICA_CHANNEL_COUNT) { printf("bad channel map entry %lu\n", (unsigned long)chan_word); result = -1; goto done; }
    bool ram_ok = verify_region(base, RING_BYTES);
    printf("SETUP ch=%u ring_addr=0x%08lx ring_frames=%d guard=%d sine_cycles=%d (%.1f Hz) "
           "need_KB_s=88.2 settle_us=%d\n", ch, (unsigned long)base, RING_FRAMES, GUARD_FRAMES,
           SINE_CYCLES, SINE_CYCLES * 44100.0 / RING_FRAMES, SELECT_SETTLE_US);
    printf("RAMCHK bank payload at ring_addr matches ring_pcm: %s\n", ram_ok ? "OK" : "MISMATCH");
    CHECK("ring_addr_maps_to_voice_sample_base", ram_ok);

    static const uint32_t sizes[] = {512, 1024, 2048, 4096};
    double kbs_burst_avg[4] = {0}, avg_us_iso[4] = {0};
    bool amb_any = false, upload_fail = false, verify_fail = false;
    double worst_dev = 0;
    uint32_t bad_state = 0;

    for (unsigned si = 0; si < 4; si++) {
        uint32_t frames = sizes[si], bytes = frames * 2u;
        uint32_t umin = UINT32_MAX, umax = 0; uint64_t usum = 0;
        double dev_max_abs = 0;

        /* Isolated uploads, ~10 ms apart, cursor read before and after each. */
        for (int rep = 0; rep < ISO_REPS; rep++) {
            uint64_t tb, ta;
            uint32_t cb = cursor_at(ch, &tb);
            uint64_t t0 = timer_us_gettime64();
            int r = afx_mem_upload(base, ring_pcm, bytes);
            uint64_t t1 = timer_us_gettime64();
            uint32_t ca = cursor_at(ch, &ta);
            if (r) { upload_fail = true; printf("  upload FAIL (%d)\n", r); }
            uint32_t us = (uint32_t)(t1 - t0);
            if (us < umin) umin = us;
            if (us > umax) umax = us;
            usum += us;
            uint32_t adv; double exp, dev;
            bool ok = cursor_dev(cb, tb, ca, ta, &adv, &exp, &dev);
            if (!ok) amb_any = true; else if (dev < 0 ? -dev > dev_max_abs : dev > dev_max_abs) dev_max_abs = dev < 0 ? -dev : dev;
            if (rep < 3)
                printf("UPLOAD size_frames=%lu rep=%d us=%lu cursor_before=%lu cursor_after=%lu "
                       "cursor_advanced=%lu expected=%.1f dev=%.1f\n",
                       (unsigned long)frames, rep, (unsigned long)us, (unsigned long)cb,
                       (unsigned long)ca, (unsigned long)adv, exp, ok ? dev : 0.0);
            afx_update();
            afx_instance_status_t st = {0};
            if (afx_instance_status(inst, &st) || st.state != AFX_PARKED) bad_state++;
            thd_sleep(5);
        }
        double avg = (double)usum / ISO_REPS;
        avg_us_iso[si] = avg;
        bool vok = verify_region(base, bytes);
        if (!vok) verify_fail = true;
        printf("SWEEP size_frames=%lu size_bytes=%lu reps=%d upload_us_min=%lu upload_us_avg=%.1f "
               "upload_us_max=%lu KB_per_sec_avg=%.1f us_per_KB=%.1f cursor_dev_max_abs=%.1f verify=%s\n",
               (unsigned long)frames, (unsigned long)bytes, ISO_REPS, (unsigned long)umin, avg,
               (unsigned long)umax, bytes * 1000.0 / avg, avg * 1000.0 / bytes, dev_max_abs,
               vok ? "OK" : "FAIL");
        if (dev_max_abs > worst_dev) worst_dev = dev_max_abs;

        /* Back-to-back bursts: sustained G2 traffic. */
        double kb_sum = 0, kb_min = 1e18, kb_max = 0;
        for (int b = 0; b < BURST_REPS; b++) {
            uint64_t tb, ta;
            uint32_t cb = cursor_at(ch, &tb);
            uint32_t call_max = 0;
            uint64_t s0 = timer_us_gettime64(), prev = s0;
            for (int k = 0; k < BURST_COUNT; k++) {
                int r = afx_mem_upload(base, ring_pcm, bytes);
                uint64_t now = timer_us_gettime64();
                if (r) upload_fail = true;
                if ((uint32_t)(now - prev) > call_max) call_max = (uint32_t)(now - prev);
                prev = now;
            }
            uint64_t total = prev - s0;
            uint32_t ca = cursor_at(ch, &ta);
            uint32_t adv; double exp, dev = 0;
            bool ok = cursor_dev(cb, tb, ca, ta, &adv, &exp, &dev);
            if (!ok) amb_any = true; else { double a = dev < 0 ? -dev : dev; if (a > worst_dev) worst_dev = a; }
            double kbs = (double)bytes * BURST_COUNT * 1000.0 / (double)total;
            kb_sum += kbs; if (kbs < kb_min) kb_min = kbs; if (kbs > kb_max) kb_max = kbs;
            printf("BURST size_frames=%lu count=%d total_us=%lu per_call_max_us=%lu sustained_KB_s=%.1f "
                   "cursor_before=%lu cursor_after=%lu cursor_advanced=%lu expected=%.1f dev=%s%.1f\n",
                   (unsigned long)frames, BURST_COUNT, (unsigned long)total, (unsigned long)call_max, kbs,
                   (unsigned long)cb, (unsigned long)ca, (unsigned long)adv, exp, ok ? "" : "amb:", ok ? dev : 0.0);
            afx_update();
            afx_instance_status_t st = {0};
            if (afx_instance_status(inst, &st) || st.state != AFX_PARKED) bad_state++;
            thd_sleep(5);
        }
        kbs_burst_avg[si] = kb_sum / BURST_REPS;
        if (!verify_region(base, bytes)) verify_fail = true;
        printf("BURST_SUMMARY size_frames=%lu sustained_KB_s_min=%.1f avg=%.1f max=%.1f headroom_vs_88.2=%.1fx\n",
               (unsigned long)frames, kb_min, kbs_burst_avg[si], kb_max, kbs_burst_avg[si] / 88.2);
    }

    /* Linearity: fit isolated avg_us = overhead + slope * bytes between 512 and 4096 frames. */
    double slope = (avg_us_iso[3] - avg_us_iso[0]) / (4096.0 * 2 - 512.0 * 2);
    double overhead = avg_us_iso[0] - slope * 512.0 * 2;
    printf("SCALING slope_us_per_byte=%.4f fixed_overhead_us=%.1f", slope, overhead);
    for (unsigned si = 0; si < 4; si++) {
        double predicted = overhead + slope * sizes[si] * 2.0;
        printf(" pred_%lu=%.0f/meas=%.0f", (unsigned long)sizes[si], predicted, avg_us_iso[si]);
    }
    printf("\n");

    CHECK("all_uploads_returned_ok", !upload_fail);
    CHECK("readback_verify", !verify_fail);
    CHECK("playback_stable", bad_state == 0);
    CHECK("cursor_tracks_wall_time_during_uploads", worst_dev < 12.0);
    CHECK("no_ambiguous_cursor_intervals", !amb_any);
    printf("NOTE worst cursor deviation %.1f samples (read resolution ~+-3 samples)\n", worst_dev);

done:
    {
        int rr = release_voice(inst, flow);
        if (rr) { printf("release FAIL (%d)\n", rr); all_ok = 0; }
        if (!result) result = rr;
    }
    afx_shutdown();
    printf("%s\n", (!result && all_ok) ? "RING_BW_SWEEP_DONE (bandwidth only; streaming viability NOT claimed)"
                                       : "AICA_RING_TEST_FAIL");
    printf("AICA_RING_TEST_END\n");
    return (!result && all_ok) ? 0 : 1;
}
