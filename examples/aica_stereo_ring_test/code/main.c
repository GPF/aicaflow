/*
 * aica_stereo_ring_test — stereo ring refill with per-start host-side offset compensation.
 *
 * The right voice starts ~18-19 samples after the left (aica-stereo-lock.md).  Per start we measure
 * d = (R cursor - L cursor), round to whole samples, and store the right ring shifted by d (ring frame f
 * holds stream sample f - d), so both channels play the same stream instant together.  Refills for both rings
 * are scheduled from the LEFT cursor only (R cursor = L + d); every poll also reads an L,R,L triplet to check
 * that premise.  Compensation alternates ON/OFF every 5 s for listening.
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


/* ===================== stereo ring ===================== */

enum {
    HALF_FRAMES = RING_FRAMES / 2,
    POLL_US = 5000, LATE_FRAMES = 512, DANGER_FRAMES = 1024,
    SESSIONS = 3, SEGMENTS = 4, SEGMENT_MS = 5000,
    CLICK_PERIOD = 11025                                       /* 0.25 s */
};

alignas(32) static int16_t half_buf[HALF_FRAMES];
alignas(32) static int16_t init_buf[RING_FRAMES + GUARD_FRAMES];

/* Stream sample at stream time n: loud click burst every 0.25 s (identical in both channels) + a quiet
 * channel-identifying tone (L 440 Hz, R 646 Hz, phase from n so it is continuous across refills). */
static int16_t stream_sample(unsigned chan, int64_t n) {
    if (n < 0) return 0;
    static const uint32_t inc[2] = {(uint32_t)(4294967296.0 * 440.0 / 44100.0), (uint32_t)(4294967296.0 * 646.0 / 44100.0)};
    int32_t v = 0;
    int64_t m = n % CLICK_PERIOD;
    if (m < 16) v += ((m & 1) ? -1 : 1) * (16000 * (int32_t)(16 - m) / 16);
    uint32_t phase = (uint32_t)((uint64_t)n * inc[chan]);
    v += (int32_t)(2500.0f * sinf(6.2831853f * ((float)phase / 4294967296.0f)));
    return (int16_t)v;
}

typedef struct {
    const char *name; uint32_t slot;
    int64_t play_abs; uint64_t prod; uint32_t gen[2];
    uint32_t refills, guard_updates, late, underruns, gen_errors, verify_errors, guard_bad, max_upload_us;
    int64_t min_margin, max_lateness; bool in_under, in_generr;
} chan_t;

static bool ram_equals(uint32_t spu_addr, const void *want, uint32_t bytes) {
    const uint8_t *w = want;
    g2_ctx_t ctx = g2_lock();
    bool ok = true;
    for (uint32_t i = 0; i < bytes && ok; i += 4) {
        uint32_t v; memcpy(&v, w + i, 4);
        if (g2_read_32(SPU_RAM_SH4 + spu_addr + i) != v) ok = false;
    }
    g2_unlock(ctx);
    return ok;
}
static bool guard_consistent(uint32_t ring) {
    g2_ctx_t ctx = g2_lock();
    bool ok = true;
    for (int i = 0; i < 2; i++)
        if (g2_read_32(SPU_RAM_SH4 + ring + i * 4) != g2_read_32(SPU_RAM_SH4 + ring + RING_FRAMES * 2 + i * 4)) ok = false;
    g2_unlock(ctx);
    return ok;
}

/* Right ring frame f holds stream sample f - d_used; left holds f. */
static int64_t stream_index(unsigned chan, int64_t ring_frame, int d_used) { return chan ? ring_frame - d_used : ring_frame; }

static int fill_ring(uint32_t ring_base, unsigned chan, int d_used) {
    for (int i = 0; i < RING_FRAMES; i++) init_buf[i] = stream_sample(chan, stream_index(chan, i, d_used));
    memcpy(init_buf + RING_FRAMES, init_buf, GUARD_FRAMES * sizeof(int16_t));
    uint32_t addr = ring_base + chan * SLOT_BYTES;
    int r = afx_mem_upload(addr, init_buf, sizeof(init_buf));
    return (r || !ram_equals(addr, init_buf, sizeof(init_buf))) ? -1 : 0;
}

/* Produce and upload one half of one ring. */
static void refill(chan_t *c, unsigned chan, uint32_t ring_base, int d_used, uint64_t K, int64_t lateness, uint64_t *busy_us) {
    uint64_t t0 = now_us();
    for (int i = 0; i < HALF_FRAMES; i++)
        half_buf[i] = stream_sample(chan, stream_index(chan, (int64_t)(K * HALF_FRAMES) + i, d_used));
    uint64_t t1 = now_us();
    uint32_t half = (uint32_t)(K & 1), ring = ring_base + c->slot;
    uint32_t addr = ring + half * HALF_FRAMES * 2;
    int ur = afx_mem_upload(addr, half_buf, HALF_FRAMES * 2);
    if (half == 0 && !ur) { ur = afx_mem_upload(ring + RING_FRAMES * 2, half_buf, GUARD_FRAMES * 2); c->guard_updates++; }
    uint64_t t2 = now_us();
    if (ur) c->verify_errors++;
    c->gen[half] = (uint32_t)K; c->prod += HALF_FRAMES; c->refills++;
    bool vok = ram_equals(addr, half_buf, 4) && ram_equals(addr + HALF_FRAMES, (uint8_t *)half_buf + HALF_FRAMES, 4) &&
               ram_equals(addr + HALF_FRAMES * 2 - 4, (uint8_t *)half_buf + HALF_FRAMES * 2 - 4, 4);
    if (c->refills <= 2 || c->refills % 128 == 0) vok = vok && ram_equals(addr, half_buf, HALF_FRAMES * 2);
    if (!vok) { c->verify_errors++; printf("VERIFY_ERROR %s refill=%lu\n", c->name, (unsigned long)c->refills); }
    if (!guard_consistent(ring)) { c->guard_bad++; printf("GUARD_MISMATCH %s\n", c->name); }
    uint32_t up = (uint32_t)(t2 - t1);
    if (up > c->max_upload_us) c->max_upload_us = up;
    if (lateness > c->max_lateness) c->max_lateness = lateness;
    if (lateness > LATE_FRAMES) c->late++;
    *busy_us += now_us() - t0;
}

/* Per-poll accounting for one channel: margin, underrun, generation ownership. */
static void account(chan_t *c) {
    int64_t margin = (int64_t)c->prod - c->play_abs;
    if (margin < c->min_margin) c->min_margin = margin;
    if (margin <= 0) { if (!c->in_under) { c->underruns++; printf("UNDERRUN %s play_abs=%lld prod=%llu\n", c->name, (long long)c->play_abs, (unsigned long long)c->prod); } c->in_under = true; }
    else c->in_under = false;
    uint64_t half = (uint64_t)(c->play_abs / HALF_FRAMES);
    if (c->gen[half & 1] != (uint32_t)half) { if (!c->in_generr) { c->gen_errors++; printf("GEN_ERROR %s half=%llu has=%lu\n", c->name, (unsigned long long)half, (unsigned long)c->gen[half & 1]); } c->in_generr = true; }
    else c->in_generr = false;
}

/* L,R,L triplet: returns the L cursor at the first read, its time, and the interpolated R-L offset. */
static bool triplet(unsigned chL, unsigned chR, uint32_t *posL, uint64_t *tL, double *d) {
    uint64_t ta, tb, tc;
    uint32_t a0 = cursor_at(chL, &ta), b = cursor_at(chR, &tb), a1 = cursor_at(chL, &tc);
    int da = wrap4096((int)a1 - (int)a0);
    if (da < 0 || da > 60 || tc <= ta) return false;
    double frac = (double)(tb - ta) / (double)(tc - ta);
    double v = (double)wrap4096((int)b - (int)a0) - (double)da * frac;
    if (v > 300.0 || v < -300.0) return false;
    *posL = a0; *tL = ta; *d = v;
    return true;
}

typedef struct { bool pass; double d_cal; } session_result_t;

static session_result_t run_session(const afx_bank_t *bank, uint32_t ring_base, int session) {
    session_result_t out = {false, 0};
    afx_asset_t flow = 0; afx_instance_t inst = 0; unsigned chL = 0, chR = 0;
    if (start_voices(bank, &flow, &inst, &chL, &chR)) return out;
    thd_sleep(30);

    /* 1. calibrate the start offset */
    stat_t cal; stat_reset(&cal);
    for (uint64_t end = now_us() + 1500000u; now_us() < end; ) { double d; if (pair_offset(chL, chR, &d)) stat_add(&cal, d); }
    const double d_cal = stat_mean(&cal);
    const int d_int = (int)floor(d_cal + 0.5);
    printf("SESSION %d CAL chL=%u chR=%u d_cal=%.3f sd=%.3f -> d_int=%d samples (%.0f us)\n", session, chL, chR,
           d_cal, stat_sd(&cal), d_int, d_int / 0.0441);
    out.d_cal = d_cal;

    /* 2. fill both rings (compensation ON), then sync to a left-ring wrap */
    int d_used = d_int;
    if (fill_ring(ring_base, 0, d_used) || fill_ring(ring_base, 1, d_used)) { printf("initial fill FAIL\n"); release_voice(inst, flow); return out; }
    uint32_t last = cursor_read(chL), pos = last; bool wrapped = false, synced = false;
    for (uint64_t dl = now_us() + 1500000u; now_us() < dl; ) {
        pos = cursor_read(chL);
        if (!wrapped && pos < last && last > 3000) wrapped = true;
        if (wrapped && pos >= 300 && pos <= 900) { synced = true; break; }
        last = pos; timer_spin_delay_us(1500);
    }
    if (!synced) { printf("could not sync to left ring wrap\n"); release_voice(inst, flow); return out; }

    chan_t C[2] = {{.name = "L", .slot = 0}, {.name = "R", .slot = SLOT_BYTES}};
    for (int k = 0; k < 2; k++) {
        C[k].prod = RING_FRAMES; C[k].gen[0] = 0; C[k].gen[1] = 1; C[k].min_margin = INT64_MAX;
    }
    int64_t play_L = pos;                                      /* the ONLY cursor used for scheduling */
    uint32_t last_pos = pos; uint64_t t_last = now_us();
    stat_t dev; stat_reset(&dev);
    uint32_t polls = 0, bad_polls = 0, outliers = 0, cursor_anoms = 0, inst_errors = 0;
    double max_dev = 0; uint64_t busy_us = 0;
    int seg_prev = -1;
    const uint64_t t_start = now_us(), t_end = t_start + (uint64_t)SEGMENTS * SEGMENT_MS * 1000u;
    uint64_t next_poll = t_start;

    while (now_us() < t_end) {
        while (now_us() < next_poll) thd_pass();
        next_poll += POLL_US;
        int seg = (int)((now_us() - t_start) / ((uint64_t)SEGMENT_MS * 1000u));
        if (seg >= SEGMENTS) seg = SEGMENTS - 1;
        if (seg != seg_prev) {
            seg_prev = seg;
            d_used = (seg & 1) ? 0 : d_int;
            printf("SEGMENT session=%d seg=%d compensation=%s  (listen: ON = centred clicks, OFF = image pulled left)\n",
                   session, seg, (seg & 1) ? "OFF" : "ON");
        }
        uint32_t posL; uint64_t tL; double d_now;
        uint64_t p0 = now_us();
        if (!triplet(chL, chR, &posL, &tL, &d_now)) { bad_polls++; continue; }
        polls++;
        uint32_t adv = posL >= last_pos ? posL - last_pos : posL + RING_FRAMES - last_pos;
        double exp_adv = (double)(tL - t_last) * 0.0441;
        if (adv > exp_adv + 40.0 || adv + 40.0 < exp_adv || tL - t_last > 80000u) cursor_anoms++;
        play_L += adv; last_pos = posL; t_last = tL;
        double e = d_now - d_cal; stat_add(&dev, e);
        double ae = e < 0 ? -e : e; if (ae > max_dev) max_dev = ae; if (ae > 3.0) outliers++;
        busy_us += now_us() - p0;

        C[0].play_abs = play_L;
        C[1].play_abs = play_L + d_int;                         /* right cursor inferred from the left one */
        account(&C[0]); account(&C[1]);
        for (unsigned c = 0; c < 2; c++) {
            for (;;) {
                uint64_t K = C[c].prod / HALF_FRAMES, free_at = (K - 1) * HALF_FRAMES;
                if (C[c].play_abs < (int64_t)free_at) break;
                refill(&C[c], c, ring_base, d_used, K, C[c].play_abs - (int64_t)free_at, &busy_us);
            }
        }
        if (polls % 5 == 0) {
            afx_instance_status_t st = {0};
            if (afx_update() < 0 || afx_instance_status(inst, &st) || st.state != AFX_PARKED) inst_errors++;
        }
    }

    /* 3. re-measure the offset at the end of the session */
    stat_t endm; stat_reset(&endm);
    for (uint64_t end = now_us() + 500000u; now_us() < end; ) { double d; if (pair_offset(chL, chR, &d)) stat_add(&endm, d); }
    const double d_end = stat_mean(&endm);
    const uint64_t t_done = now_us();

    printf("SESSION %d SUMMARY duration_ms=%lu polls=%lu bad_polls=%lu\n", session, (unsigned long)((t_done - t_start) / 1000u),
           (unsigned long)polls, (unsigned long)bad_polls);
    for (int k = 0; k < 2; k++)
        printf("  %s refills=%lu guard_updates=%lu min_margin=%lld max_lateness=%lld late=%lu underruns=%lu gen_errors=%lu "
               "verify_errors=%lu guard_mismatch=%lu max_upload_us=%lu\n", C[k].name, (unsigned long)C[k].refills,
               (unsigned long)C[k].guard_updates, (long long)C[k].min_margin, (long long)C[k].max_lateness,
               (unsigned long)C[k].late, (unsigned long)C[k].underruns, (unsigned long)C[k].gen_errors,
               (unsigned long)C[k].verify_errors, (unsigned long)C[k].guard_bad, (unsigned long)C[k].max_upload_us);
    printf("  offset_stability: mean_dev=%.3f sd=%.3f max_abs_dev=%.2f outliers(>3)=%lu d_start=%.3f d_end=%.3f (delta %.3f)\n",
           stat_mean(&dev), stat_sd(&dev), max_dev, (unsigned long)outliers, d_cal, d_end, d_end - d_cal);
    printf("  cursor_anomalies=%lu instance_errors=%lu sh4_busy_pct=%.2f\n", (unsigned long)cursor_anoms,
           (unsigned long)inst_errors, 100.0 * (double)busy_us / (double)(t_done - t_start));

    bool ok = true;
    for (int k = 0; k < 2; k++)
        ok = ok && C[k].underruns == 0 && C[k].late == 0 && C[k].gen_errors == 0 && C[k].verify_errors == 0 &&
             C[k].guard_bad == 0 && C[k].min_margin >= DANGER_FRAMES;
    double dm = stat_mean(&dev); if (dm < 0) dm = -dm;
    double dd = d_end - d_cal; if (dd < 0) dd = -dd;
    ok = ok && cursor_anoms == 0 && inst_errors == 0 && dm <= 0.5 && dd <= 0.5 && polls > 0 && outliers * 200 <= polls;
    printf("SESSION %d %s\n", session, ok ? "PASS" : "FAIL");
    int rr = release_voice(inst, flow);
    if (rr) { printf("release FAIL (%d)\n", rr); ok = false; }
    out.pass = ok;
    return out;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    alignas(32) static const unsigned char firmware[] = {
#embed "../../../firmware/aicaflow.drv"
    };
    afx_bank_t bank = {0};
    int result = 0, all_ok = 1;
    printf("STEREO_RING_BEGIN sessions=%d segments=%d segment_ms=%d\n", SESSIONS, SEGMENTS, SEGMENT_MS);
    make_rings();
    result = afx_init(firmware, sizeof(firmware));
    if (!result) result = load_bank(&bank);
    if (result) { printf("setup FAIL (%d)\n", result); all_ok = 0; goto done; }
    const uint32_t ring_base = afx_asset_addr(bank.asset);

    double dcal[SESSIONS]; int ran = 0;
    for (int s = 0; s < SESSIONS; s++) {
        session_result_t r = run_session(&bank, ring_base, s + 1);
        dcal[s] = r.d_cal; ran++;
        if (!r.pass) all_ok = 0;
    }
    double lo = dcal[0], hi = dcal[0];
    for (int s = 1; s < ran; s++) { if (dcal[s] < lo) lo = dcal[s]; if (dcal[s] > hi) hi = dcal[s]; }
    printf("STEREO_RING_RECAL d_cal per session:");
    for (int s = 0; s < ran; s++) printf(" %.3f", dcal[s]);
    printf("  spread=%.3f\n", hi - lo);
    if (hi - lo > 1.5) { printf("CHECK recalibration_consistent: FAIL\n"); all_ok = 0; }
    else printf("CHECK recalibration_consistent: PASS\n");
done:
    afx_shutdown();
    printf("%s\nSTEREO_RING_END\n", all_ok ? "STEREO_RING_PASS" : "STEREO_RING_FAIL");
    return all_ok ? 0 : 1;
}
