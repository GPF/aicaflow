/*
 * aica_stereo_cost_test — SH-4 cost of a production-like stereo ring.
 * Stereo ring logic as in aica_stereo_ring_test, minus the diagnostics (no per-sample test signal, no read-back or
 * L,R,L triplet in steady state).  Busy time is bucketed with a cheap integer TMU2 tick counter.
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
__attribute__((unused)) static double stat_sd(const stat_t *s) {
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


/* ===================== lean stereo ring ===================== */

enum { HALF_FRAMES = RING_FRAMES / 2, DANGER_FRAMES = 1024, TMU_TICKS_PER_SEC = 12468720 };

static inline uint64_t raw_ticks(void) {           /* integer TMU2 ticks since boot: no double math in the hot path */
    timer_val_t v = __dreamcast_get_ticks();
    return (uint64_t)v.secs * TMU_TICKS_PER_SEC + v.ticks;
}
static double ticks_to_us(uint64_t t) { return (double)t * (1000000.0 / TMU_TICKS_PER_SEC); }

alignas(32) static int16_t pat[2][HALF_FRAMES];        /* base pattern, period = one half ring */
alignas(32) static int16_t out_half[2][HALF_FRAMES];   /* what is uploaded: L = pat, R = pat rotated by the offset */
alignas(32) static int16_t init_buf[RING_FRAMES + GUARD_FRAMES];

/* Integer cycles per half-ring so every half is identical; a click at frame 0 of each half (21.5 Hz buzz). */
static void make_patterns(void) {
    static const float cycles[2] = {10.0f, 15.0f};
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < HALF_FRAMES; i++) {
            int32_t v = (int32_t)(5000.0f * sinf(6.2831853f * cycles[c] * (float)i / HALF_FRAMES));
            if (i < 16) v += ((i & 1) ? -1 : 1) * (14000 * (16 - i) / 16);
            pat[c][i] = (int16_t)v;
        }
}
/* Right ring frame f holds stream sample f - d; the stream has period HALF_FRAMES, so R is a rotation of the pattern. */
static void prepare_halves(int d_int) {
    memcpy(out_half[0], pat[0], sizeof(out_half[0]));
    for (int i = 0; i < HALF_FRAMES; i++) {
        int j = (i - d_int) % HALF_FRAMES; if (j < 0) j += HALF_FRAMES;
        out_half[1][i] = pat[1][j];
    }
}
static int fill_ring(uint32_t ring_base, unsigned chan) {
    memcpy(init_buf, out_half[chan], sizeof(out_half[chan]));
    memcpy(init_buf + HALF_FRAMES, out_half[chan], sizeof(out_half[chan]));
    memcpy(init_buf + RING_FRAMES, init_buf, GUARD_FRAMES * sizeof(int16_t));
    return afx_mem_upload(ring_base + chan * SLOT_BYTES, init_buf, sizeof(init_buf));
}
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
static bool ring_ok(uint32_t ring_base, unsigned chan) {      /* end-of-run verify: both halves + guard */
    memcpy(init_buf, out_half[chan], sizeof(out_half[chan]));
    memcpy(init_buf + HALF_FRAMES, out_half[chan], sizeof(out_half[chan]));
    memcpy(init_buf + RING_FRAMES, init_buf, GUARD_FRAMES * sizeof(int16_t));
    return ram_equals(ring_base + chan * SLOT_BYTES, init_buf, sizeof(init_buf));
}

typedef struct {
    uint32_t slot;
    int64_t play_abs; uint64_t prod; uint32_t gen[2];
    uint32_t refills, late, underruns, gen_errors, upload_errors;
    int64_t min_margin, max_lateness; bool in_under, in_generr;
    uint64_t up_sum, up_max;                                  /* raw ticks */
} chan_t;

static void account(chan_t *c) {
    int64_t margin = (int64_t)c->prod - c->play_abs;
    if (margin < c->min_margin) c->min_margin = margin;
    if (margin <= 0) { if (!c->in_under) c->underruns++; c->in_under = true; } else c->in_under = false;
    uint64_t half = (uint64_t)(c->play_abs / HALF_FRAMES);
    if (c->gen[half & 1] != (uint32_t)half) { if (!c->in_generr) c->gen_errors++; c->in_generr = true; } else c->in_generr = false;
}

typedef struct {
    int poll_us, seconds;
    double busy_pct, cursor_pct, upload_pct, status_pct, other_pct;
    bool ok; double d_cal;
} cost_t;

static cost_t run_cost(const afx_bank_t *bank, uint32_t ring_base, int poll_us, int seconds, int label) {
    cost_t out = {poll_us, seconds, 0, 0, 0, 0, 0, false, 0};
    afx_asset_t flow = 0; afx_instance_t inst = 0; unsigned chL = 0, chR = 0;
    if (start_voices(bank, &flow, &inst, &chL, &chR)) return out;
    thd_sleep(30);
    stat_t cal; stat_reset(&cal);
    for (uint64_t end = now_us() + 1500000u; now_us() < end; ) { double d; if (pair_offset(chL, chR, &d)) stat_add(&cal, d); }
    const double d_cal = stat_mean(&cal);
    const int d_int = (int)floor(d_cal + 0.5);
    out.d_cal = d_cal;
    prepare_halves(d_int);
    if (fill_ring(ring_base, 0) || fill_ring(ring_base, 1)) { printf("fill FAIL\n"); release_voice(inst, flow); return out; }
    uint32_t last = cursor_read(chL), pos = last; bool wrapped = false, synced = false;
    for (uint64_t dl = now_us() + 1500000u; now_us() < dl; ) {
        pos = cursor_read(chL);
        if (!wrapped && pos < last && last > 3000) wrapped = true;
        if (wrapped && pos >= 300 && pos <= 900) { synced = true; break; }
        last = pos; timer_spin_delay_us(1500);
    }
    if (!synced) { printf("sync FAIL\n"); release_voice(inst, flow); return out; }

    const int64_t late_limit = (int64_t)(poll_us * 0.0441) + 300;      /* poll interval + upload/jitter allowance */
    chan_t C[2] = {{.slot = 0}, {.slot = SLOT_BYTES}};
    for (int k = 0; k < 2; k++) { C[k].prod = RING_FRAMES; C[k].gen[0] = 0; C[k].gen[1] = 1; C[k].min_margin = INT64_MAX; }
    int64_t play_L = pos; uint32_t last_pos = pos;
    const uint64_t period = (uint64_t)((double)poll_us * TMU_TICKS_PER_SEC / 1000000.0);
    uint64_t t_last = raw_ticks(), t_start = t_last, next = t_start;
    const uint64_t t_end = t_start + (uint64_t)seconds * TMU_TICKS_PER_SEC;
    uint64_t cur_sum = 0, cur_max = 0, up_calls = 0, status_sum = 0, active_sum = 0, active_max = 0;
    uint32_t polls = 0, cursor_anoms = 0, inst_errors = 0;

    while (raw_ticks() < t_end) {
        while (raw_ticks() < next) thd_pass();                     /* idle (not counted as work) */
        next += period;
        uint64_t a0 = raw_ticks();

        uint32_t p = cursor_read(chL);
        uint64_t a1 = raw_ticks();
        uint32_t adv = p >= last_pos ? p - last_pos : p + RING_FRAMES - last_pos;
        int64_t exp = (int64_t)((a1 - t_last) * 44100u / TMU_TICKS_PER_SEC);
        if (((int64_t)adv - exp > 40) || (exp - (int64_t)adv > 40)) cursor_anoms++;
        play_L += adv; last_pos = p; t_last = a1;
        C[0].play_abs = play_L; C[1].play_abs = play_L + d_int;
        account(&C[0]); account(&C[1]);

        for (unsigned c = 0; c < 2; c++)
            for (;;) {
                uint64_t K = C[c].prod / HALF_FRAMES; int64_t free_at = (int64_t)((K - 1) * HALF_FRAMES);
                if (C[c].play_abs < free_at) break;
                int64_t lateness = C[c].play_abs - free_at;
                if (lateness > C[c].max_lateness) C[c].max_lateness = lateness;
                if (lateness > late_limit) C[c].late++;
                uint32_t half = (uint32_t)(K & 1), ring = ring_base + C[c].slot;
                uint64_t u0 = raw_ticks();
                int ur = afx_mem_upload(ring + half * HALF_FRAMES * 2, out_half[c], HALF_FRAMES * 2);
                if (!ur && half == 0) ur = afx_mem_upload(ring + RING_FRAMES * 2, out_half[c], GUARD_FRAMES * 2);
                uint64_t u1 = raw_ticks();
                if (ur) C[c].upload_errors++;
                C[c].up_sum += u1 - u0; if (u1 - u0 > C[c].up_max) C[c].up_max = u1 - u0;
                up_calls++;
                C[c].gen[half] = (uint32_t)K; C[c].prod += HALF_FRAMES; C[c].refills++;
            }
        cur_sum += a1 - a0; if (a1 - a0 > cur_max) cur_max = a1 - a0;

        uint64_t s0 = raw_ticks();
        ++polls;
        if (polls % (25000 / poll_us > 0 ? 25000 / poll_us : 1) == 0) {      /* ~every 25 ms, at least every poll */
            afx_instance_status_t st = {0};
            if (afx_update() < 0 || afx_instance_status(inst, &st) || st.state != AFX_PARKED) inst_errors++;
        }
        uint64_t s1 = raw_ticks();
        status_sum += s1 - s0;
        uint64_t act = s1 - a0; active_sum += act; if (act > active_max) active_max = act;
    }
    const uint64_t t_done = raw_ticks(), wall = t_done - t_start;

    /* end-of-run checks (outside the measured loop): offset unchanged, ring contents intact */
    stat_t endm; stat_reset(&endm);
    for (uint64_t end = now_us() + 400000u; now_us() < end; ) { double d; if (pair_offset(chL, chR, &d)) stat_add(&endm, d); }
    double dd = stat_mean(&endm) - d_cal; if (dd < 0) dd = -dd;
    bool rings = ring_ok(ring_base, 0) && ring_ok(ring_base, 1);

    uint64_t up_total = C[0].up_sum + C[1].up_sum, up_peak = C[0].up_max > C[1].up_max ? C[0].up_max : C[1].up_max;
    uint64_t other = active_sum - cur_sum - up_total - status_sum;
    double secs = (double)wall / TMU_TICKS_PER_SEC;
    out.cursor_pct = 100.0 * (double)cur_sum / wall;  out.upload_pct = 100.0 * (double)up_total / wall;
    out.status_pct = 100.0 * (double)status_sum / wall; out.other_pct = 100.0 * (double)other / wall;
    out.busy_pct = 100.0 * (double)active_sum / wall;

    printf("COST run=%d poll_ms=%d duration_s=%.2f polls=%lu refills_per_channel=%lu/%lu d_cal=%.3f d_int=%d\n", label, poll_us / 1000, secs,
           (unsigned long)polls, (unsigned long)C[0].refills, (unsigned long)C[1].refills, d_cal, d_int);
    printf("COST_BUCKETS_percent_of_wall cursor=%.3f upload=%.3f afx_update_status=%.3f other_bookkeeping=%.3f TOTAL_BUSY=%.3f\n",
           out.cursor_pct, out.upload_pct, out.status_pct, out.other_pct, out.busy_pct);
    printf("COST_PER_EVENT_us cursor_read avg=%.1f max=%.1f | upload(4096B)+guard avg=%.1f max=%.1f | poll_active avg=%.1f max=%.1f\n",
           ticks_to_us(cur_sum) / polls, ticks_to_us(cur_max), up_calls ? ticks_to_us(up_total) / up_calls : 0.0, ticks_to_us(up_peak),
           ticks_to_us(active_sum) / polls, ticks_to_us(active_max));
    printf("COST_HEALTH late=%lu/%lu underruns=%lu/%lu gen_errors=%lu/%lu upload_errors=%lu/%lu min_margin_L=%lld min_margin_R=%lld "
           "max_lateness_L=%lld max_lateness_R=%lld cursor_anomalies=%lu instance_errors=%lu\n",
           (unsigned long)C[0].late, (unsigned long)C[1].late, (unsigned long)C[0].underruns, (unsigned long)C[1].underruns,
           (unsigned long)C[0].gen_errors, (unsigned long)C[1].gen_errors, (unsigned long)C[0].upload_errors, (unsigned long)C[1].upload_errors,
           (long long)C[0].min_margin, (long long)C[1].min_margin, (long long)C[0].max_lateness, (long long)C[1].max_lateness,
           (unsigned long)cursor_anoms, (unsigned long)inst_errors);
    printf("COST_END_CHECK offset_delta=%.3f rings_intact=%d late_limit_frames=%lld\n", dd, rings ? 1 : 0, (long long)late_limit);

    bool ok = true;
    for (int k = 0; k < 2; k++)
        ok = ok && C[k].underruns == 0 && C[k].late == 0 && C[k].gen_errors == 0 && C[k].upload_errors == 0 && C[k].min_margin >= DANGER_FRAMES;
    ok = ok && cursor_anoms == 0 && inst_errors == 0 && dd <= 0.5 && rings;
    printf("COST run=%d %s\n", label, ok ? "PASS" : "FAIL");
    int rr = release_voice(inst, flow);
    if (rr) { printf("release FAIL (%d)\n", rr); ok = false; }
    out.ok = ok;
    return out;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    alignas(32) static const unsigned char firmware[] = {
#embed "../../../firmware/aicaflow.drv"
    };
    afx_bank_t bank = {0};
    int result = 0, all_ok = 1;
    printf("STEREO_COST_BEGIN\n");
    make_rings(); make_patterns();
    result = afx_init(firmware, sizeof(firmware));
    if (!result) result = load_bank(&bank);
    if (result) { printf("setup FAIL (%d)\n", result); all_ok = 0; goto done; }
    const uint32_t ring_base = afx_asset_addr(bank.asset);

    static const struct { int poll_us, seconds; } plan[] = {{5000, 60}, {10000, 20}, {20000, 20}};
    cost_t res[3];
    for (int i = 0; i < 3; i++) { res[i] = run_cost(&bank, ring_base, plan[i].poll_us, plan[i].seconds, i + 1); if (!res[i].ok) all_ok = 0; }
    for (int i = 0; i < 3; i++)
        printf("STEREO_COST poll=%dms: SH-4 busy %.2f%% (cursor %.2f + upload %.2f + status %.2f + other %.2f) %s\n",
               plan[i].poll_us / 1000, res[i].busy_pct, res[i].cursor_pct, res[i].upload_pct, res[i].status_pct, res[i].other_pct,
               res[i].ok ? "healthy" : "UNHEALTHY");
done:
    afx_shutdown();
    printf("%s\nSTEREO_COST_END\n", all_ok ? "STEREO_COST_PASS" : "STEREO_COST_FAIL");
    return all_ok ? 0 : 1;
}
