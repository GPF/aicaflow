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
 * Phase 2 (Mode A): 60 s refill loop - phase-continuous 440 Hz sine, per-half
 * generation tracking, cursor polled every 5 ms, a half is rewritten only after the
 * cursor has fully left it, guard frames mirrored whenever ring frames 0..3 change.
 *
 * NOT here: deliberate underrun, stereo, mGBA load, any public API.
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


/* KOS's timer_us_gettime64() is not linear: arch_timer_gettime() converts TMU2 ticks
 * at 80 ns each, but the real tick is 4 / 49.87488 MHz = 80.2 ns and TMU2 reloads once
 * per real second (12,468,720 ticks).  So the reading runs 0.25% slow and snaps forward
 * ~2.5 ms every second (110 AICA frames) - the once-per-second "cursor anomaly".
 * Use the tick count against the true tick rate instead. */
#define TMU2_TICKS_PER_SEC 12468720.0
static inline uint64_t now_us(void) {
    timer_val_t v = __dreamcast_get_ticks();
    return (uint64_t)v.secs * 1000000u + (uint64_t)((double)v.ticks * (1000000.0 / TMU2_TICKS_PER_SEC));
}

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
    fields[AFX_FIELD_DIRECT] = 0x0f00u;   /* pan 0 = centre (15 is a hard pan; user heard right ear only) */
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
static uint32_t g_read_us;      /* duration of the most recent cursor_at() */
static uint32_t cursor_at(unsigned ch, uint64_t *t_mid) {
    uint64_t a = now_us();
    uint32_t pos = cursor_read(ch);
    uint64_t b = now_us();
    *t_mid = (a + b) / 2;
    g_read_us = (uint32_t)(b - a);
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


/* ------------------------------------------------------------------------- */
/* Mode A: live refill correctness (mono, 60 s, continuous 440 Hz sine).     */
/* ------------------------------------------------------------------------- */

#ifndef MODE_A_MS
#define MODE_A_MS 60000
#endif
#ifndef MODE_U_ARM_MS
#define MODE_U_ARM_MS 12000
#endif
#ifndef MODE_U_POST_MS
#define MODE_U_POST_MS 12000
#endif
#define MODE_U_MAX_MS 45000
enum {
    HALF_FRAMES = RING_FRAMES / 2,
    POLL_US = 5000,
    LATE_FRAMES = 512,          /* refill started > 11.6 ms after its half became free */
    DANGER_FRAMES = 1024,       /* < 23 ms of valid audio ahead of the playhead */
    ANOMALY_FRAMES = 32         /* cursor advance vs wall clock tolerance per poll */
};

alignas(32) static int16_t half_buf[HALF_FRAMES];
alignas(32) static int16_t init_buf[RING_FRAMES + GUARD_FRAMES];
static float g_phase;

static void gen_frames(int16_t *out, uint32_t n) {
    const float two_pi = 6.2831853f, inc = two_pi * 440.0f / 44100.0f;
    float ph = g_phase;
    for (uint32_t i = 0; i < n; i++) {
        out[i] = (int16_t)(12000.0f * sinf(ph));
        ph += inc;
        if (ph >= two_pi) ph -= two_pi;
    }
    g_phase = ph;
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

/* Guard frames (ring[4096..4099]) must always equal ring[0..3] in AICA RAM. */
static bool guard_consistent(uint32_t base) {
    g2_ctx_t ctx = g2_lock();
    bool ok = true;
    for (int i = 0; i < 2; i++)
        if (g2_read_32(SPU_RAM_SH4 + base + i * 4) !=
            g2_read_32(SPU_RAM_SH4 + base + RING_FRAMES * 2 + i * 4)) ok = false;
    g2_unlock(ctx);
    return ok;
}


#ifdef MODEA_DENSE
/* Dense read window: read the cursor back-to-back (~100 us each) and log time vs
 * position, to tell a real cursor freeze from a timer discontinuity. */
static void dense_window(unsigned ch, uint64_t t_end_us, int idx) {
    static uint64_t tt[512]; static uint32_t pp[512];
    uint32_t n = 0;
    while (n < 512) {
        uint32_t pos = cursor_read(ch);
        uint64_t t = now_us();
        tt[n] = t; pp[n] = pos; n++;
        if (t >= t_end_us) break;
    }
    double sum_exp = 0, sum_adv = 0, worst = 0; uint32_t worst_i = 1, max_dt_i = 1, notable = 0;
    uint64_t max_dt = 0;
    for (uint32_t i = 1; i < n; i++) {
        uint64_t dt = tt[i] - tt[i - 1];
        uint32_t adv = pp[i] >= pp[i - 1] ? pp[i] - pp[i - 1] : pp[i] + RING_FRAMES - pp[i - 1];
        double exp = (double)dt * 0.0441, res = (double)adv - exp;
        sum_exp += exp; sum_adv += adv;
        if ((res < 0 ? -res : res) > (worst < 0 ? -worst : worst)) { worst = res; worst_i = i; }
        if (dt > max_dt) { max_dt = dt; max_dt_i = i; }
    }
    printf("DENSE window=%d reads=%lu span_us=%lu sum_adv=%.0f sum_expected=%.1f net_deficit=%.1f "
           "worst_residual=%.1f at_i=%lu max_dt_us=%lu at_i=%lu\n", idx, (unsigned long)n,
           (unsigned long)(tt[n - 1] - tt[0]), sum_adv, sum_exp, sum_exp - sum_adv, worst,
           (unsigned long)worst_i, (unsigned long)max_dt, (unsigned long)max_dt_i);
    uint32_t lo = worst_i > 6 ? worst_i - 6 : 1, hi = worst_i + 6 < n ? worst_i + 6 : n - 1;
    for (uint32_t i = lo; i <= hi; i++) {
        uint64_t dt = tt[i] - tt[i - 1];
        uint32_t adv = pp[i] >= pp[i - 1] ? pp[i] - pp[i - 1] : pp[i] + RING_FRAMES - pp[i - 1];
        printf("  d%d i=%lu t_rel_us=%lu pos=%lu dt_us=%lu adv=%lu expected=%.1f residual=%.1f\n", idx,
               (unsigned long)i, (unsigned long)(tt[i] - tt[0]), (unsigned long)pp[i], (unsigned long)dt,
               (unsigned long)adv, (double)dt * 0.0441, (double)adv - (double)dt * 0.0441);
        notable++;
    }
    (void)notable;
}
#endif

static int run_ring(afx_instance_t inst, uint32_t base, unsigned ch, bool inject) {
    const char *tag = inject ? "MODE_U" : "MODE_A";
    printf("%s_BEGIN duration_ms=%d half_frames=%d poll_us=%d late_frames=%d danger_frames=%d\n",
           tag, inject ? MODE_U_MAX_MS : MODE_A_MS, HALF_FRAMES, POLL_US, LATE_FRAMES, DANGER_FRAMES);

    /* Fill the whole ring (+guard) with stream frames 0..4095, phase-continuous. */
    g_phase = 0.0f;
    gen_frames(init_buf, RING_FRAMES);
    memcpy(init_buf + RING_FRAMES, init_buf, GUARD_FRAMES * sizeof(int16_t));
    int r = afx_mem_upload(base, init_buf, sizeof(init_buf));
    if (r || !ram_equals(base, init_buf, sizeof(init_buf))) {
        printf("MODE_A_FAIL initial fill (%d)\n", r);
        return 0;
    }

    /* Sync to a ring wrap so lap accounting starts with the cursor near frame 0. */
    uint64_t t_last; uint32_t last = cursor_at(ch, &t_last), pos = last;
    uint64_t sync_deadline = now_us() + 1000000u;
    bool synced = false;
    while (now_us() < sync_deadline) {
        uint64_t t;
        thd_pass();
        pos = cursor_at(ch, &t);
        if (pos < last && pos < 512) { t_last = t; synced = true; break; }
        last = pos; t_last = t;
        timer_spin_delay_us(1500);
    }
    if (!synced) { printf("MODE_A_FAIL could not sync to ring wrap\n"); return 0; }

#ifdef MODEA_NO_REFILL
    uint64_t play_abs = pos, prod = (uint64_t)1 << 40;   /* never an underrun */
#else
    uint64_t play_abs = pos, prod = RING_FRAMES;
#endif
    const uint32_t aica_tick0 = afx_status_timer_ticks();   /* AICA-domain clock (ARM7 timer-A FIQ) */
    const uint64_t aica_frame0 = play_abs;
    uint32_t gen[2] = {0, 1};
    last = pos;

    uint32_t refills = 0, guard_updates = 0, late = 0, underruns = 0, gen_errors = 0,
             cursor_anoms = 0, inst_errors = 0, guard_bad = 0, verify_errors = 0,
             poll_stalls = 0, polls = 0, max_upload_us = 0, max_gen_us = 0, max_poll_gap_us = 0;
    int64_t min_margin = (int64_t)(prod - play_abs);
    int64_t max_lateness = 0;
    uint64_t busy_us = 0, upload_total_us = 0;
    bool in_underrun = false, in_generr = false;
    /* Mode U (deliberate underrun) state */
    uint64_t skip_K = 0, t_recovery_ok = 0;
    bool armed = false, skip_printed = false, warned = false, detected = false, recovery_ok = false;
    uint32_t intentional = 0, intentional_generr = 0, post_ok = 0, recov_printed = 0;
    int64_t detect_latency = -1, warn_margin = -1, min_margin_episode = INT64_MAX;
    struct { uint32_t pos, adv, rd_us; uint32_t dt_us; double exp; uint64_t t_ms; } hist[8];
    uint32_t hist_n = 0, trail = 0, anomaly_dumps = 0;
#ifdef MODEA_DENSE
    uint64_t dense_t_event = 0; int dense_done = 0;
#endif

    uint64_t t_start = now_us(), next_poll = t_start + POLL_US, next_progress = t_start + 5000000u;
    uint64_t t_prev_poll = t_start;
    uint64_t wall_end = t_start + (uint64_t)(inject ? MODE_U_MAX_MS : MODE_A_MS) * 1000u;

    while (now_us() < wall_end) {
#ifdef MODEA_SPIN_POLL
        while (now_us() < next_poll) { }
#else
        while (now_us() < next_poll) thd_pass();
#endif
#ifdef MODEA_DENSE
        if (dense_t_event && dense_done < 3) {
            uint64_t target = dense_t_event + (uint64_t)(dense_done + 1) * 1000000u;
            if (now_us() + 10000u >= target) {
                dense_window(ch, target + 4000u, dense_done + 1);
                dense_done++;
                next_poll = now_us() + POLL_US;
                if (dense_done == 3) wall_end = 0;       /* done: leave the loop */
            }
        }
#endif
        uint64_t now0 = now_us();
        if (now0 > next_poll + 20000u) poll_stalls++;
        if (now0 - t_prev_poll > max_poll_gap_us) max_poll_gap_us = (uint32_t)(now0 - t_prev_poll);
        t_prev_poll = now0;
        next_poll += POLL_US;
        polls++;

        /* 1. cursor, unwrapped */
        uint64_t tm;
        pos = cursor_at(ch, &tm);
        busy_us += now_us() - now0;
        uint32_t adv = pos >= last ? pos - last : pos + RING_FRAMES - last;
        double exp_adv = (double)(tm - t_last) * 0.0441;
        if (pos >= RING_FRAMES || tm - t_last > 80000u ||
            ((double)adv - exp_adv > ANOMALY_FRAMES || exp_adv - (double)adv > ANOMALY_FRAMES)) {
            cursor_anoms++;
#ifndef MODEA_QUIET
            if (cursor_anoms <= 5)
                printf("CURSOR_ANOMALY pos=%lu last=%lu adv=%lu expected=%.1f dt_us=%lu\n",
                       (unsigned long)pos, (unsigned long)last, (unsigned long)adv, exp_adv,
                       (unsigned long)(tm - t_last));
#endif
        }
        {
            uint32_t hi = hist_n++ & 7;
            hist[hi].pos = pos; hist[hi].adv = adv; hist[hi].rd_us = g_read_us;
            hist[hi].dt_us = (uint32_t)(tm - t_last); hist[hi].exp = exp_adv; hist[hi].t_ms = (tm - t_start) / 1000u;
            bool is_anom = pos >= RING_FRAMES || tm - t_last > 80000u ||
                ((double)adv - exp_adv > ANOMALY_FRAMES || exp_adv - (double)adv > ANOMALY_FRAMES);
#ifdef MODEA_DENSE
            if (is_anom && !dense_t_event) dense_t_event = tm;
#endif
#ifdef MODEA_QUIET
            if (is_anom) anomaly_dumps = 99;
#endif
            if (is_anom && anomaly_dumps < 4) {
                anomaly_dumps++; trail = 3;
                printf("ANOMALY_CONTEXT #%lu (oldest first, last entry is the anomaly)\n", (unsigned long)anomaly_dumps);
                for (uint32_t k = 6; k > 0; k--) {
                    uint32_t j = (hist_n - 1 - (k - 1)) & 7;
                    if (hist_n >= k) printf("  t_ms=%llu pos=%lu adv=%lu expected=%.1f dt_us=%lu read_us=%lu\n",
                        (unsigned long long)hist[j].t_ms, (unsigned long)hist[j].pos, (unsigned long)hist[j].adv,
                        hist[j].exp, (unsigned long)hist[j].dt_us, (unsigned long)hist[j].rd_us);
                }
            } else if (trail) {
                trail--;
                printf("  +after t_ms=%llu pos=%lu adv=%lu expected=%.1f dt_us=%lu read_us=%lu\n",
                       (unsigned long long)hist[hi].t_ms, (unsigned long)pos, (unsigned long)adv, exp_adv,
                       (unsigned long)hist[hi].dt_us, (unsigned long)g_read_us);
            }
        }
        play_abs += adv; last = pos; t_last = tm;

        /* 2. margin / underrun / generation ownership */
        int64_t margin = (int64_t)(prod - play_abs);
        bool episode = inject && skip_printed && !recovery_ok;
        if (episode) { if (margin < min_margin_episode) min_margin_episode = margin; }
        else if (margin < min_margin) min_margin = margin;
        uint64_t cur_half = play_abs / HALF_FRAMES;
        if (inject && skip_printed && !detected && !warned && margin > 0 && margin <= LATE_FRAMES) {
            warned = true; warn_margin = margin;
            printf("UNDERRUN_WARNING margin=%lld cursor_frames=%lu ring_half=%u pending_generation=%llu (stale data not yet reached)\n",
                   (long long)margin, (unsigned long)pos, (unsigned)(cur_half & 1), (unsigned long long)skip_K);
        }
        if (margin <= 0) {
            if (!in_underrun) {
                if (inject && skip_printed && !detected && cur_half == skip_K) {
                    detected = true; intentional++;
                    detect_latency = (int64_t)(play_abs - skip_K * HALF_FRAMES);
                    printf("UNDERRUN cursor=%lu half=%u expected_gen=%llu actual_gen=%lu detect_latency_frames=%lld warned_before=%d\n",
                           (unsigned long)pos, (unsigned)(cur_half & 1), (unsigned long long)cur_half,
                           (unsigned long)gen[cur_half & 1], (long long)detect_latency, warned ? 1 : 0);
                } else {
                    underruns++;
                    printf("UNDERRUN_UNEXPECTED play_abs=%llu prod=%llu\n", (unsigned long long)play_abs, (unsigned long long)prod);
                }
            }
            in_underrun = true;
        } else in_underrun = false;
        if (gen[cur_half & 1] != (uint32_t)cur_half) {
            if (!in_generr) {
                if (inject && skip_printed && cur_half == skip_K && !recovery_ok) intentional_generr++;
                else { gen_errors++; printf("GEN_ERROR cursor_half=%llu ring_half=%u has_generation=%lu\n", (unsigned long long)cur_half, (unsigned)(cur_half & 1), (unsigned long)gen[cur_half & 1]); }
            }
            in_generr = true;
        } else in_generr = false;

        /* 3. refill every half the cursor has fully left */
#ifndef MODEA_NO_REFILL
        for (;;) {
            uint64_t K = prod / HALF_FRAMES;                  /* next stream half to produce */
            uint64_t free_at = (K - 1) * HALF_FRAMES;         /* cursor leaves stream half K-2 here */
            if (play_abs < free_at) break;
            int64_t lateness = (int64_t)(play_abs - free_at);
            if (inject && !armed && (now_us() - t_start) >= (uint64_t)MODE_U_ARM_MS * 1000u) {
                armed = true; skip_K = K;
                printf("UNDERRUN_ARMED generation=%llu half=%u\n", (unsigned long long)K, (unsigned)(K & 1));
            }
            if (inject && armed && !detected && K == skip_K) {
                if (!skip_printed) {
                    skip_printed = true;
                    printf("REFILL_SKIPPED half=%u generation=%llu lateness_frames=%lld margin=%lld\n",
                           (unsigned)(K & 1), (unsigned long long)K, (long long)lateness, (long long)(prod - play_abs));
                }
                break;                                      /* withheld on purpose */
            }
            if (lateness > max_lateness) max_lateness = lateness;
            if (lateness > LATE_FRAMES && !(inject && armed && !recovery_ok)) late++;

            uint64_t g0 = now_us();
            gen_frames(half_buf, HALF_FRAMES);
            uint64_t g1 = now_us();
            uint32_t half = (uint32_t)(K & 1);
            uint32_t addr = base + half * HALF_FRAMES * 2;
            int ur = afx_mem_upload(addr, half_buf, HALF_FRAMES * 2);
            if (half == 0 && !ur) {                            /* ring[0..3] changed: mirror guard */
                ur = afx_mem_upload(base + RING_FRAMES * 2, half_buf, GUARD_FRAMES * 2);
                guard_updates++;
            }
            uint64_t g2t = now_us();
            if (ur) { verify_errors++; printf("UPLOAD_ERROR %d\n", ur); }
            gen[half] = (uint32_t)K;
            prod += HALF_FRAMES;
            refills++;
            if (inject && detected && !recovery_ok) {
                if (recov_printed < 2) {
                    recov_printed++;
                    printf("RECOVERY_REFILL n=%lu K=%llu half=%lu cursor_minus_half_start=%lld (negative: catch-up, cursor not there yet) margin_after=%lld\n",
                           (unsigned long)refills, (unsigned long long)K, (unsigned long)(K & 1),
                           (long long)(play_abs - (K) * HALF_FRAMES), (long long)(prod - play_abs));
                } else if ((int64_t)(prod - play_abs) >= DANGER_FRAMES) post_ok++;
            }

            /* spot checks: first/middle/last word of the half; guard mirror; occasional full verify */
            bool vok = ram_equals(addr, half_buf, 4) &&
                       ram_equals(addr + HALF_FRAMES, (uint8_t *)half_buf + HALF_FRAMES, 4) &&
                       ram_equals(addr + HALF_FRAMES * 2 - 4, (uint8_t *)half_buf + HALF_FRAMES * 2 - 4, 4);
            if (refills <= 2 || refills % 128 == 0) vok = vok && ram_equals(addr, half_buf, HALF_FRAMES * 2);
            if (!vok) { verify_errors++; printf("VERIFY_ERROR refill=%lu half=%lu\n", (unsigned long)refills, (unsigned long)half); }
            if (!guard_consistent(base)) { guard_bad++; printf("GUARD_MISMATCH refill=%lu\n", (unsigned long)refills); }
            uint64_t g3 = now_us();

            uint32_t up_us = (uint32_t)(g2t - g1), gen_us = (uint32_t)(g1 - g0);
            if (up_us > max_upload_us) max_upload_us = up_us;
            if (gen_us > max_gen_us) max_gen_us = gen_us;
            upload_total_us += up_us;
            busy_us += g3 - g0;
            if (refills <= 3)
                printf("REFILL n=%lu K=%llu half=%lu lateness=%lld gen_us=%lu upload_us=%lu margin_after=%lld\n",
                       (unsigned long)refills, (unsigned long long)K, (unsigned long)half, (long long)lateness,
                       (unsigned long)gen_us, (unsigned long)up_us, (long long)(prod - play_abs));
        }
#endif

        if (inject && detected && !recovery_ok && post_ok >= 3 && !in_underrun && !in_generr &&
            (int64_t)(prod - play_abs) >= DANGER_FRAMES) {
            recovery_ok = true; t_recovery_ok = now_us();
            printf("RECOVERY_OK refills_after_recovery=%lu margin=%lld generation_resynced=1\n",
                   (unsigned long)post_ok, (long long)(prod - play_abs));
            wall_end = t_recovery_ok + (uint64_t)MODE_U_POST_MS * 1000u;
        }

        /* 4. AFX instance health (every 5th poll = 25 ms) */
#ifndef MODEA_NO_AFX_UPDATE
        if (polls % 5 == 0) {
            afx_instance_status_t st = {0};
            if (afx_update() < 0 || afx_instance_status(inst, &st) || st.state != AFX_PARKED) inst_errors++;
        }
#endif

        if (now_us() >= next_progress) {
            next_progress += 5000000u;
            printf("PROGRESS t_s=%lu refills=%lu min_margin=%lld max_lateness=%lld late=%lu underruns=%lu anomalies=%lu\n",
                   (unsigned long)((now_us() - t_start) / 1000000u), (unsigned long)refills,
                   (long long)min_margin, (long long)max_lateness, (unsigned long)late,
                   (unsigned long)underruns, (unsigned long)cursor_anoms);
        }
    }

    uint64_t t_end = now_us();
    uint32_t duration_ms = (uint32_t)((t_end - t_start) / 1000u);
    double ratio = (double)play_abs / ((double)(t_end - t_start) * 0.0441);
    uint32_t expected_refills = (uint32_t)((uint64_t)duration_ms * 441u / 10u / HALF_FRAMES);
    bool completed = inject ? (armed && detected && recovery_ok && (t_end - t_recovery_ok) + 200000u >= (uint64_t)MODE_U_POST_MS * 1000u)
                            : (duration_ms >= (uint32_t)(MODE_A_MS - 100) && refills + 3 >= expected_refills);
    bool pass = completed && underruns == 0 && late == 0 && gen_errors == 0 && cursor_anoms == 0 &&
                inst_errors == 0 && guard_bad == 0 && verify_errors == 0 && min_margin >= DANGER_FRAMES;
    if (inject) pass = pass && intentional == 1 && intentional_generr <= 1 && warned && recovery_ok;

    {
        /* Cross-check with the AICA-domain tick counter: needs no SH-4 timer at all. */
        uint32_t ticks = afx_status_timer_ticks() - aica_tick0;
        uint64_t frames = play_abs - aica_frame0;
        printf("AICA_CLOCK_CHECK ticks=%lu cursor_frames=%llu frames_per_tick=%.4f implied_tick_hz=%.3f "
               "(1000.000 if ticks were exact ms; 1002.273 if timer A reload 212 -> 44 samples)\n",
               (unsigned long)ticks, (unsigned long long)frames,
               ticks ? (double)frames / ticks : 0.0, frames ? (double)ticks * 44100.0 / (double)frames : 0.0);
    }
    printf("%s_SUMMARY\n", tag);
    printf("duration_ms=%lu\n", (unsigned long)duration_ms);
    printf("refills=%lu (expected~%lu)\n", (unsigned long)refills, (unsigned long)expected_refills);
    printf("polls=%lu poll_stalls=%lu max_poll_gap_us=%lu\n", (unsigned long)polls, (unsigned long)poll_stalls, (unsigned long)max_poll_gap_us);
    printf("min_margin_samples=%lld (danger<%d)\n", (long long)min_margin, DANGER_FRAMES);
    printf("max_lateness_samples=%lld (late>%d)\n", (long long)max_lateness, LATE_FRAMES);
    printf("max_upload_us=%lu avg_upload_us=%.1f max_gen_us=%lu\n", (unsigned long)max_upload_us,
           refills ? (double)upload_total_us / refills : 0.0, (unsigned long)max_gen_us);
    printf("late_refills=%lu\n", (unsigned long)late);
    printf("underruns=%lu\n", (unsigned long)underruns);
    printf("generation_errors=%lu\n", (unsigned long)gen_errors);
    printf("guard_updates=%lu guard_mismatches=%lu\n", (unsigned long)guard_updates, (unsigned long)guard_bad);
    printf("verify_errors=%lu\n", (unsigned long)verify_errors);
    printf("cursor_errors=%lu\n", (unsigned long)cursor_anoms);
    printf("instance_errors=%lu\n", (unsigned long)inst_errors);
    printf("sh4_cursor_refill_busy_pct=%.2f aica_vs_sh4_clock_ratio=%.5f\n",
           100.0 * (double)busy_us / (double)(t_end - t_start), ratio);
    if (inject) {
        printf("armed_generation=%llu intentional_underruns=%lu intentional_generation_errors=%lu\n",
               (unsigned long long)skip_K, (unsigned long)intentional, (unsigned long)intentional_generr);
        printf("warning_before_stale=%d warn_margin_frames=%lld detect_latency_frames=%lld\n",
               warned ? 1 : 0, (long long)warn_margin, (long long)detect_latency);
        printf("recovery_ok=%d post_recovery_ms=%lu refills_after_recovery=%lu\n", recovery_ok ? 1 : 0,
               (unsigned long)(recovery_ok ? (t_end - t_recovery_ok) / 1000u : 0), (unsigned long)post_ok);
        printf("min_margin_normal=%lld min_margin_during_episode=%lld\n", (long long)min_margin, (long long)min_margin_episode);
        printf("(underruns/generation_errors above count UNEXPECTED events only)\n");
    }
    printf("%s_%s\n", tag, pass ? "PASS" : "FAIL");
    return pass;
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

    printf("AICA_RING_TEST_BEGIN phases=bandwidth_sweep,mode_a,mode_u\n");
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

#ifdef RING_SKIP_SWEEP
#define NSIZES 0
#else
#define NSIZES 4
#endif
    for (unsigned si = 0; si < NSIZES; si++) {
        uint32_t frames = sizes[si], bytes = frames * 2u;
        uint32_t umin = UINT32_MAX, umax = 0; uint64_t usum = 0;
        double dev_max_abs = 0;

        /* Isolated uploads, ~10 ms apart, cursor read before and after each. */
        for (int rep = 0; rep < ISO_REPS; rep++) {
            uint64_t tb, ta;
            uint32_t cb = cursor_at(ch, &tb);
            uint64_t t0 = now_us();
            int r = afx_mem_upload(base, ring_pcm, bytes);
            uint64_t t1 = now_us();
            uint32_t ca = cursor_at(ch, &ta);
            if (r) { upload_fail = true; printf("  upload FAIL (%d)\n", r); }
            uint32_t us = (uint32_t)(t1 - t0);
            if (us < umin) umin = us;
            if (us > umax) umax = us;
            usum += us;
            uint32_t adv; double exp, dev;
            bool ok = cursor_dev(cb, tb, ca, ta, &adv, &exp, &dev);
            if (!ok) amb_any = true; else if (dev < 0 ? -dev > dev_max_abs : dev > dev_max_abs) dev_max_abs = dev < 0 ? -dev : dev;
            {
                double ad = ok ? (dev < 0 ? -dev : dev) : 0.0;
                static int outlier_prints;
                if (rep >= 1 && (us > umin + umin / 4 + 20 || ad > 12.0) && outlier_prints++ < 12)
                    printf("OUTLIER size_frames=%lu rep=%d us=%lu (min so far %lu) cursor_before=%lu cursor_after=%lu "
                           "advanced=%lu expected=%.1f dev=%.1f t_gap_us=%lu\n",
                           (unsigned long)frames, rep, (unsigned long)us, (unsigned long)umin,
                           (unsigned long)cb, (unsigned long)ca, (unsigned long)adv, exp, ok ? dev : 0.0,
                           (unsigned long)(ta - tb));
            }
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
            uint64_t s0 = now_us(), prev = s0;
            for (int k = 0; k < BURST_COUNT; k++) {
                int r = afx_mem_upload(base, ring_pcm, bytes);
                uint64_t now = now_us();
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

    printf("SWEEP_RESULT %s\n", all_ok ? "PASS" : "FAIL (see CHECK lines)");
    /* Modes A and U depend on correct uploads and read-back, not on sweep timing outliers. */
    if (ram_ok && !upload_fail && !verify_fail && bad_state == 0) {
#ifndef RING_SKIP_MODE_A
        if (!run_ring(inst, base, ch, false)) all_ok = 0;
#endif
#ifndef RING_SKIP_MODE_U
        if (!run_ring(inst, base, ch, true)) all_ok = 0;
#endif
    } else { printf("RING_MODES_SKIPPED (uploads/readback/state not clean)\n"); all_ok = 0; }

done:
    {
        int rr = release_voice(inst, flow);
        if (rr) { printf("release FAIL (%d)\n", rr); all_ok = 0; }
        if (!result) result = rr;
    }
    afx_shutdown();
    printf("%s\n", (!result && all_ok) ? "AICA_RING_TEST_DONE (behaviour under real load NOT claimed)"
                                       : "AICA_RING_TEST_FAIL");
    printf("AICA_RING_TEST_END\n");
    return (!result && all_ok) ? 0 : 1;
}
