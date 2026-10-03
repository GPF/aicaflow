/*
 * aica_stereo_lock_test — are two voices started by one AFX flow sample-phase locked?
 *
 * Offset = (right cursor) - (left cursor), in samples, measured by reading L, R, L and interpolating
 * L to the time of the R read (all timestamps from the corrected clock, same procedure for each read, so
 * its fixed bias cancels).  Cursor read: w32 select of 0xa070280c, 50 us settle (aica-cursor-probe.md).
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
    SLOT_BYTES = (RING_BYTES + 31) & ~31,                    /* 8224 */
    BANK_PAYLOAD = 2 * SLOT_BYTES,
    BANK_ID_LOW = 0x4f455453u,                               /* "STEO" */
    PITCH_NATIVE = 0x0000,
    MIX_PLAY = 0x1424,
    PAN_LEFT = 0x1f, PAN_RIGHT = 0x0f,                       /* AICA DIPAN: 0x1f hard left, 0x0f hard right, 0 centre */
    START_RUNS = 6
};

/* Corrected clock (KOS timer_us_gettime64 steps +2.5 ms each second). */
#define TMU2_TICKS_PER_SEC 12468720.0
static inline uint64_t now_us(void) {
    timer_val_t v = __dreamcast_get_ticks();
    return (uint64_t)v.secs * 1000000u + (uint64_t)((double)v.ticks * (1000000.0 / TMU2_TICKS_PER_SEC));
}

alignas(32) static uint8_t bank_file[AFX_BANK_HEADER_BYTES + BANK_PAYLOAD];
alignas(32) static uint8_t ring_pcm[2][SLOT_BYTES];

static void make_rings(void) {
    static const float cycles[2] = {40.0f, 60.0f};          /* 430.7 Hz left, 646.0 Hz right */
    memset(ring_pcm, 0, sizeof(ring_pcm));
    for (int v = 0; v < 2; v++)
        for (int i = 0; i < RING_FRAMES + GUARD_FRAMES; i++) {
            int16_t s = (int16_t)(9000.0f * sinf(6.2831853f * cycles[v] * (float)(i % RING_FRAMES) / RING_FRAMES));
            ring_pcm[v][i * 2] = (uint8_t)(s & 0xff);
            ring_pcm[v][i * 2 + 1] = (uint8_t)((uint16_t)s >> 8);
        }
}

static int load_bank(afx_bank_t *bank) {
    memset(bank_file, 0, sizeof(bank_file));
    afx_write32(bank_file, AFX_BANK_MAGIC); afx_write32(bank_file + 4, AFX_BANK_VERSION);
    afx_write32(bank_file + 8, BANK_ID_LOW); afx_write32(bank_file + 12, 0);
    afx_write32(bank_file + 16, AFX_BANK_HEADER_BYTES); afx_write32(bank_file + 20, BANK_PAYLOAD);
    afx_write32(bank_file + 24, sizeof(bank_file));
    for (int v = 0; v < 2; v++) memcpy(bank_file + AFX_BANK_HEADER_BYTES + v * SLOT_BYTES, ring_pcm[v], SLOT_BYTES);
    return afx_bank_load_memory(bank, bank_file, sizeof(bank_file));
}

static int wait_state(afx_instance_t inst, uint32_t wanted, uint32_t timeout_ms) {
    uint64_t deadline = timer_ms_gettime64() + timeout_ms;
    do {
        afx_instance_status_t st;
        if (afx_update() < 0 || afx_instance_status(inst, &st)) return -1;
        if (st.state == wanted) return 0;
        thd_sleep(1);
    } while (timer_ms_gettime64() < deadline);
    return -1;
}

static int append(uint8_t *stream, uint32_t cap, uint32_t *written, const afx_event_t *ev, const uint16_t *vals) {
    uint32_t bytes;
    if (*written > cap || afx_encode_event(stream + *written, cap - *written, ev, vals, &bytes)) return -1;
    *written += bytes;
    return 0;
}

/* One flow, two channels / two setups, both started by NOTE_PL at tick 0, then PARK. */
static int build_stereo_flow(const afx_bank_t *bank, afx_asset_t *out_flow) {
    uint16_t fields[2][AFX_FIELD_COUNT];
    memset(fields, 0, sizeof(fields));
    for (unsigned v = 0; v < 2; v++) {
        uint32_t offset = v * SLOT_BYTES;
        fields[v][AFX_FIELD_CONTROL] = (uint16_t)(0x0200u | (offset >> 16));
        fields[v][AFX_FIELD_SAMPLE_LOW] = (uint16_t)offset;
        fields[v][AFX_FIELD_LOOP_END] = RING_FRAMES - 1;
        fields[v][AFX_FIELD_ENV_AD] = 0x001f;
        fields[v][AFX_FIELD_ENV_DR] = 0x001f;
        fields[v][AFX_FIELD_DIRECT] = (uint16_t)(0x0f00u | (v == 0 ? PAN_LEFT : PAN_RIGHT));
        fields[v][AFX_FIELD_MIX] = 0x0024;
        for (unsigned f = AFX_FIELD_FILTER_LEVEL0; f <= AFX_FIELD_FILTER_LEVEL4; ++f) fields[v][f] = 0x1fff;
    }
    uint8_t stream[32];
    uint32_t written = 0;
    uint16_t note[] = {PITCH_NATIVE, MIX_PLAY};
    afx_event_t ev = {.opcode = AFX_OP_NOTE_PL, .channel = 0, .setup = 0, .mask = AFX_NOTE_PL_MASK};
    if (append(stream, sizeof(stream), &written, &ev, note)) return -1;
    ev.channel = 1; ev.setup = 1;
    if (append(stream, sizeof(stream), &written, &ev, note)) return -1;
    if (written >= sizeof(stream)) return -1;
    stream[written++] = AFX_OP_PARK;

    const uint32_t image_at = 128;                           /* 80 header + 2 relocations (24) -> 32-byte aligned */
    uint8_t file[128 + 2 * AFX_SETUP_BYTES + 32] = {0};
    uint32_t setup_bytes = 2 * AFX_SETUP_BYTES, image_bytes = setup_bytes + written;
    afx_write32(file, AFX_FILE_MAGIC); afx_write32(file + 4, AFX_FILE_VERSION);
    afx_write32(file + 8, image_at + image_bytes); afx_write32(file + 12, AFX_FLAG_CONTROLLED);
    afx_write32(file + 16, image_at); afx_write32(file + 20, image_bytes);
    afx_write32(file + 24, setup_bytes); afx_write32(file + 28, written);
    afx_write32(file + 36, 2); afx_write32(file + 40, bank->id.low); afx_write32(file + 44, bank->id.high);
    afx_write32(file + 48, 80); afx_write32(file + 52, 2);
    afx_write32(file + 64, 2);                               /* required channels */
    afx_write32(file + 68, AFX_TICK_RATE_NUM); afx_write32(file + 72, AFX_TICK_RATE_DEN);
    for (unsigned v = 0; v < 2; v++) {
        afx_write32(file + 80 + v * 12, v * AFX_SETUP_BYTES);
        afx_write32(file + 84 + v * 12, v * SLOT_BYTES);
        afx_write32(file + 88 + v * 12, RING_BYTES);
        for (unsigned f = 0; f < AFX_FIELD_COUNT; ++f)
            afx_write16(file + image_at + v * AFX_SETUP_BYTES + f * 2u, fields[v][f]);
    }
    memcpy(file + image_at + setup_bytes, stream, written);
    afx_write32(file + 32, afx_control_id(file + image_at, image_bytes));
    return afx_bank_flow_upload(bank, file, image_at + image_bytes, out_flow);
}

static int release_voice(afx_instance_t inst, afx_asset_t f) {
    int r = 0;
    if (inst) {
        r = afx_instance_stop(inst);
        if (!r) r = wait_state(inst, AFX_DONE, 1000);
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

static uint32_t cursor_read(unsigned channel) {
    g2_ctx_t ctx = g2_lock();
    uint32_t w = g2_read_32(MON_SEL_WORD);
    g2_write_32(MON_SEL_WORD, (w & ~0xff00u) | ((uint32_t)channel << 8));
    timer_spin_delay_us(50);
    uint32_t pos = g2_read_32(MON_POS_ADDR) & 0xffffu;
    g2_unlock(ctx);
    return pos;
}
static uint32_t cursor_at(unsigned ch, uint64_t *t_mid) {
    uint64_t a = now_us();
    uint32_t pos = cursor_read(ch);
    uint64_t b = now_us();
    *t_mid = (a + b) / 2;
    return pos;
}
static int wrap4096(int v) { v %= RING_FRAMES; if (v >= RING_FRAMES / 2) v -= RING_FRAMES; if (v < -RING_FRAMES / 2) v += RING_FRAMES; return v; }

/* Right-minus-left cursor offset in samples (sub-sample, from L,R,L interpolation). false if implausible. */
static bool pair_offset(unsigned chL, unsigned chR, double *out) {
    uint64_t ta, tb, tc;
    uint32_t a0 = cursor_at(chL, &ta), b = cursor_at(chR, &tb), a1 = cursor_at(chL, &tc);
    int da = wrap4096((int)a1 - (int)a0);
    if (da < 0 || da > 60 || tc <= ta) return false;
    double frac = (double)(tb - ta) / (double)(tc - ta);
    double d = (double)wrap4096((int)b - (int)a0) - (double)da * frac;
    if (d > 300.0 || d < -300.0) return false;
    *out = d;
    return true;
}

typedef struct { double sum, sumsq, min, max; uint32_t n, bad; } stat_t;
static void stat_reset(stat_t *s) { s->sum = s->sumsq = 0; s->min = 1e9; s->max = -1e9; s->n = s->bad = 0; }
static void stat_add(stat_t *s, double d) { s->sum += d; s->sumsq += d * d; if (d < s->min) s->min = d; if (d > s->max) s->max = d; s->n++; }
static double stat_mean(const stat_t *s) { return s->n ? s->sum / s->n : 0.0; }
static double stat_sd(const stat_t *s) {
    if (s->n < 2) return 0.0;
    double m = s->sum / s->n, v = s->sumsq / s->n - m * m;
    return v > 0 ? sqrt(v) : 0.0;
}

static int start_voices(const afx_bank_t *bank, afx_asset_t *flow, afx_instance_t *inst, unsigned *chL, unsigned *chR) {
    int r = build_stereo_flow(bank, flow);
    if (r) { printf("build_stereo_flow FAIL (%d)\n", r); return r; }
    r = afx_instance_activate(*flow, inst);
    if (!r) r = wait_state(*inst, AFX_PARKED, 1000);
    if (r) { printf("activate/park FAIL (%d)\n", r); return r; }
    g2_ctx_t c = g2_lock();
    *chL = g2_read_32(SPU_RAM_SH4 + AFX_CHANNEL_MAP_ARENA_ADDR);
    *chR = g2_read_32(SPU_RAM_SH4 + AFX_CHANNEL_MAP_ARENA_ADDR + 4);
    g2_unlock(c);
    if (*chL >= AFX_AICA_CHANNEL_COUNT || *chR >= AFX_AICA_CHANNEL_COUNT || *chL == *chR) {
        printf("bad channels L=%u R=%u\n", *chL, *chR); return -1;
    }
    return 0;
}

#define CHECK(name, ok) do { printf("CHECK %s: %s\n", name, (ok) ? "PASS" : "FAIL"); if (!(ok)) all_ok = 0; } while (0)

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    alignas(32) static const unsigned char firmware[] = {
#embed "../../../firmware/aicaflow.drv"
    };
    afx_bank_t bank = {0};
    int result = 0, all_ok = 1;
    printf("STEREO_LOCK_BEGIN\n");
    make_rings();
    result = afx_init(firmware, sizeof(firmware));
    if (!result) result = load_bank(&bank);
    if (result) { printf("setup FAIL (%d)\n", result); all_ok = 0; goto done; }
    const uint32_t ring_base = afx_asset_addr(bank.asset);

    /* ---- Phase 1: start skew over independent starts ---- */
    double run_mean[START_RUNS]; int runs_ok = 0;
    for (int run = 0; run < START_RUNS; run++) {
        afx_asset_t flow = 0; afx_instance_t inst = 0; unsigned chL = 0, chR = 0;
        if (start_voices(&bank, &flow, &inst, &chL, &chR)) { all_ok = 0; break; }
        thd_sleep(30);                                        /* ~300 ms: let both settle into the loop */
        stat_t st; stat_reset(&st);
        uint64_t end = now_us() + 1500000u;
        while (now_us() < end) { double d; if (pair_offset(chL, chR, &d)) stat_add(&st, d); else st.bad++; }
        if (run == 0) {                                          /* bias control: same channel as L and R => ~0 */
            stat_t ctl; stat_reset(&ctl);
            uint64_t cend = now_us() + 500000u;
            while (now_us() < cend) { double d; if (pair_offset(chL, chL, &d)) stat_add(&ctl, d); }
            printf("STEREO_CONTROL same_channel_offset mean=%.3f sd=%.3f n=%lu (expect ~0)\n", stat_mean(&ctl), stat_sd(&ctl), (unsigned long)ctl.n);
            CHECK("measurement_bias_control_near_zero", stat_mean(&ctl) > -0.5 && stat_mean(&ctl) < 0.5);
        }
        run_mean[run] = stat_mean(&st);
        printf("STEREO_START run=%d chL=%u chR=%u offset_R_minus_L mean=%.3f sd=%.3f min=%.2f max=%.2f n=%lu bad=%lu\n",
               run + 1, chL, chR, stat_mean(&st), stat_sd(&st), st.min, st.max, (unsigned long)st.n, (unsigned long)st.bad);
        runs_ok++;
        int rr = release_voice(inst, flow);
        if (rr) { printf("release FAIL (%d)\n", rr); all_ok = 0; break; }
    }
    if (runs_ok == START_RUNS) {
        double lo = run_mean[0], hi = run_mean[0];
        for (int i = 1; i < START_RUNS; i++) { if (run_mean[i] < lo) lo = run_mean[i]; if (run_mean[i] > hi) hi = run_mean[i]; }
        printf("STEREO_START_SUMMARY runs=%d offset_min=%.3f offset_max=%.3f spread=%.3f samples (%.1f us)\n",
               START_RUNS, lo, hi, hi - lo, (hi - lo) / 0.0441);
        /* The two observed values differ by exactly one sample-clock quantum: allow 1.5 samples. */
        CHECK("start_offset_stable_within_1p5_samples", hi - lo <= 1.5);
    }

    /* ---- Phases 2 and 3: drift over 20 s, without and with continuous ring uploads ---- */
    for (int load = 0; load < 2; load++) {
        afx_asset_t flow = 0; afx_instance_t inst = 0; unsigned chL = 0, chR = 0;
        if (start_voices(&bank, &flow, &inst, &chL, &chR)) { all_ok = 0; break; }
        thd_sleep(30);
        double first = 0, worst_dev = 0, worst_sd = 0; int windows = 0;
        uint64_t t0 = now_us(), next_load = t0;
        uint32_t uploads = 0, upload_fail = 0, state_bad = 0;
        printf("STEREO_%s chL=%u chR=%u (20 windows of 1 s)\n", load ? "LOAD" : "STEADY", chL, chR);
        for (int w = 0; w < 20; w++) {
            stat_t st; stat_reset(&st);
            uint64_t wend = t0 + (uint64_t)(w + 1) * 1000000u;
            while (now_us() < wend) {
                double d; if (pair_offset(chL, chR, &d)) stat_add(&st, d); else st.bad++;
                if (load && now_us() >= next_load) {          /* ~2 x 4 KB every 46 ms: the stereo refill cadence */
                    next_load += 46440;
                    for (int v = 0; v < 2; v++) {
                        if (afx_mem_upload(ring_base + v * SLOT_BYTES, ring_pcm[v], 4096)) upload_fail++;
                        uploads++;
                    }
                }
            }
            double m = stat_mean(&st);
            if (w == 0) first = m;
            double dev = m - first; if (dev < 0) dev = -dev;
            if (dev > worst_dev) worst_dev = dev;
            if (stat_sd(&st) > worst_sd) worst_sd = stat_sd(&st);
            windows++;
            if (w < 3 || w == 19 || dev > 0.5)
                printf("  w=%d mean=%.3f sd=%.3f min=%.2f max=%.2f n=%lu bad=%lu\n", w, m, stat_sd(&st), st.min, st.max,
                       (unsigned long)st.n, (unsigned long)st.bad);
            afx_update();
            afx_instance_status_t ist = {0};
            if (afx_instance_status(inst, &ist) || ist.state != AFX_PARKED) state_bad++;
        }
        printf("STEREO_%s_SUMMARY windows=%d first_mean=%.3f worst_window_deviation=%.3f worst_sd=%.3f uploads=%lu upload_fail=%lu state_bad=%lu\n",
               load ? "LOAD" : "STEADY", windows, first, worst_dev, worst_sd, (unsigned long)uploads,
               (unsigned long)upload_fail, (unsigned long)state_bad);
        CHECK(load ? "locked_under_upload_load" : "locked_steady_state", worst_dev <= 0.5 && worst_sd <= 1.2 && upload_fail == 0 && state_bad == 0);
        int rr = release_voice(inst, flow);
        if (rr) { printf("release FAIL (%d)\n", rr); all_ok = 0; }
    }

done:
    afx_shutdown();
    printf("%s\nSTEREO_LOCK_END\n", all_ok ? "STEREO_LOCK_DONE" : "STEREO_LOCK_FAIL");
    return all_ok ? 0 : 1;
}
