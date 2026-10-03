/*
 * aica_flow_timing_test — is an authored AFX WAIT of N ticks really N ms of AICA time?
 *
 * Flow: NOTE_PL (native pitch) -> WAIT32 N -> PATCH pitch x2 -> WAIT16 500 -> KEYOFF -> END.
 * The voice loops a sine ring.  The cursor (frames since key-on, unwrapped) is the clock:
 * the pitch change shows as the poll where the per-poll advance doubles.  Frames at that point
 * = AICA audio time for N authored ticks.  1000 Hz ticks: 44.1 N frames; 44-sample ticks
 * (timer A reload 212, 1002.27 Hz): 44.0 N frames.  The SH-4 timer is used only to pace polls.
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
    BANK_ID_LOW = 0x4d495446u,                 /* "FTIM" */
    PITCH_NATIVE = 0x0000, PITCH_2X = 0x0800,
    MIX_PLAY = 0x1424,
    POLL_US = 1500,
    TAIL_TICKS = 500
};

/* Corrected clock (KOS timer_us_gettime64 steps +2.5 ms each second); pacing only. */
#define TMU2_TICKS_PER_SEC 12468720.0
static inline uint64_t now_us(void) {
    timer_val_t v = __dreamcast_get_ticks();
    return (uint64_t)v.secs * 1000000u + (uint64_t)((double)v.ticks * (1000000.0 / TMU2_TICKS_PER_SEC));
}

alignas(32) static uint8_t bank_file[AFX_BANK_HEADER_BYTES + BANK_BYTES];

static int load_bank(afx_bank_t *bank) {
    memset(bank_file, 0, sizeof(bank_file));
    afx_write32(bank_file, AFX_BANK_MAGIC); afx_write32(bank_file + 4, AFX_BANK_VERSION);
    afx_write32(bank_file + 8, BANK_ID_LOW); afx_write32(bank_file + 12, 0);
    afx_write32(bank_file + 16, AFX_BANK_HEADER_BYTES); afx_write32(bank_file + 20, BANK_BYTES);
    afx_write32(bank_file + 24, sizeof(bank_file));
    uint8_t *pcm = bank_file + AFX_BANK_HEADER_BYTES;
    for (int i = 0; i < RING_FRAMES + GUARD_FRAMES; i++) {
        int16_t s = (int16_t)(9000.0f * sinf(6.2831853f * 40.0f * (float)(i % RING_FRAMES) / RING_FRAMES));
        pcm[i * 2] = (uint8_t)(s & 0xff);
        pcm[i * 2 + 1] = (uint8_t)((uint16_t)s >> 8);
    }
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

static int build_flow(const afx_bank_t *bank, uint32_t wait_ticks, afx_asset_t *out_flow) {
    uint16_t fields[AFX_FIELD_COUNT] = {0};
    fields[AFX_FIELD_CONTROL] = 0x0200u;
    fields[AFX_FIELD_SAMPLE_LOW] = 0;
    fields[AFX_FIELD_LOOP_END] = RING_FRAMES - 1;
    fields[AFX_FIELD_ENV_AD] = 0x001f;
    fields[AFX_FIELD_ENV_DR] = 0x001f;
    fields[AFX_FIELD_DIRECT] = 0x0f00u;
    fields[AFX_FIELD_MIX] = 0x0024;
    for (unsigned f = AFX_FIELD_FILTER_LEVEL0; f <= AFX_FIELD_FILTER_LEVEL4; ++f) fields[f] = 0x1fff;

    uint8_t stream[64];
    uint32_t written = 0;
    uint16_t note[] = {PITCH_NATIVE, MIX_PLAY}, pitch2[] = {PITCH_2X};
    afx_event_t ev = {.opcode = AFX_OP_NOTE_PL, .channel = 0, .setup = 0, .mask = AFX_NOTE_PL_MASK};
    if (append(stream, sizeof(stream), &written, &ev, note)) return -1;
    ev = (afx_event_t){.opcode = AFX_OP_WAIT32, .wait = wait_ticks};
    if (append(stream, sizeof(stream), &written, &ev, NULL)) return -1;
    ev = (afx_event_t){.opcode = AFX_OP_PATCH, .channel = 0, .mask = 1u << AFX_FIELD_PITCH};
    if (append(stream, sizeof(stream), &written, &ev, pitch2)) return -1;
    ev = (afx_event_t){.opcode = AFX_OP_WAIT16, .wait = TAIL_TICKS};
    if (append(stream, sizeof(stream), &written, &ev, NULL)) return -1;
    ev = (afx_event_t){.opcode = AFX_OP_KEYOFF, .channel = 0};
    if (append(stream, sizeof(stream), &written, &ev, NULL) || written >= sizeof(stream)) return -1;
    stream[written++] = AFX_OP_END;

    uint8_t file[96 + AFX_SETUP_BYTES + 64] = {0};
    uint32_t image_bytes = AFX_SETUP_BYTES + written;
    afx_write32(file, AFX_FILE_MAGIC); afx_write32(file + 4, AFX_FILE_VERSION);
    afx_write32(file + 8, 96 + image_bytes); afx_write32(file + 12, 0);           /* one-shot flow */
    afx_write32(file + 16, 96); afx_write32(file + 20, image_bytes);
    afx_write32(file + 24, AFX_SETUP_BYTES); afx_write32(file + 28, written);
    afx_write32(file + 36, 1); afx_write32(file + 40, bank->id.low); afx_write32(file + 44, bank->id.high);
    afx_write32(file + 48, 80); afx_write32(file + 52, 1);
    afx_write32(file + 64, 1); afx_write32(file + 68, AFX_TICK_RATE_NUM); afx_write32(file + 72, AFX_TICK_RATE_DEN);   /* AICA Timer-A time base */
    afx_write32(file + 80, 0); afx_write32(file + 84, 0); afx_write32(file + 88, RING_BYTES);
    for (unsigned f = 0; f < AFX_FIELD_COUNT; ++f) afx_write16(file + 96 + f * 2u, fields[f]);
    memcpy(file + 96 + AFX_SETUP_BYTES, stream, written);
    afx_write32(file + 32, afx_control_id(file + 96, image_bytes));
    return afx_bank_flow_upload(bank, file, 96 + image_bytes, out_flow);
}

static int release_voice(afx_instance_t inst, afx_asset_t f) {
    int r = 0;
    if (inst) {
        r = wait_state(inst, AFX_DONE, 3000);
        if (r) { (void)afx_instance_stop(inst); r = wait_state(inst, AFX_DONE, 1000); }
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

/* Returns 1 on a clean measurement; fills the result fields. */
static int measure(const afx_bank_t *bank, uint32_t seconds, double *out_hz, int64_t *out_frames) {
    /* Author the wait in REAL time through the shared time base (the fix under test). */
    const uint32_t wait_ticks = (uint32_t)afx_usec_to_ticks((uint64_t)seconds * 1000000u);
    afx_asset_t flow = 0; afx_instance_t inst = 0;
    int r = build_flow(bank, wait_ticks, &flow);
    if (r) { printf("build_flow FAIL (%d)\n", r); return 0; }

    uint32_t idle = 0;
    unsigned ch = 0;
    uint32_t ticks_start = 0;
    int ok = 0;

    /* Channel of the (to be) first instance: arena 0 entry 0 of the fresh map; idle cursor first. */
    r = afx_instance_activate(flow, &inst);
    if (r) { printf("activate FAIL (%d)\n", r); afx_asset_free(flow); return 0; }
    { g2_ctx_t c = g2_lock(); ch = g2_read_32(SPU_RAM_SH4 + AFX_CHANNEL_MAP_ARENA_ADDR); g2_unlock(c); }
    if (ch >= AFX_AICA_CHANNEL_COUNT) { printf("bad channel %u\n", ch); goto out; }

    /* Start: first cursor reading that differs from the idle reading taken just now.
     * (activation already queued; the NOTE fires on the next AICA tick, ~1 ms later) */
    idle = cursor_read(ch);
    uint64_t t_wait_end = now_us() + 3000000u;
    uint32_t pos = idle;
    while (now_us() < t_wait_end) {
        pos = cursor_read(ch);
        if (pos != idle) break;
    }
    if (pos == idle) { printf("voice never started (cursor stuck at %lu)\n", (unsigned long)idle); goto out; }
    ticks_start = afx_status_timer_ticks();

    /* F = frames since key-on (cursor starts at 0 at key-on), unwrapped. */
    uint64_t F = pos, F_prev = 0, next = now_us() + POLL_US;
    uint32_t last = pos, polls = 0, ticks_at_change = 0;
    double base_sum = 0; uint32_t base_n = 0;
    int64_t F_change_prev = -1, F_change_now = -1;
    uint32_t change_adv = 0; double change_base = 0;
    uint64_t guard_end = now_us() + (uint64_t)(wait_ticks + 3000u) * 1000u;
    printf("FLOWTIME_START wait_ticks=%lu ch=%u idle_pos=%lu first_pos=%lu ticks_start=%lu\n",
           (unsigned long)wait_ticks, ch, (unsigned long)idle, (unsigned long)pos, (unsigned long)ticks_start);

    while (now_us() < guard_end) {
        while (now_us() < next) { }
        next += POLL_US;
        uint32_t p = cursor_read(ch);
        uint32_t adv = p >= last ? p - last : p + RING_FRAMES - last;
        polls++;
        F_prev = F; F += adv; last = p;
        if (base_n >= 16 && adv > (uint32_t)(1.25 * (base_sum / base_n))) {
            F_change_prev = (int64_t)F_prev; F_change_now = (int64_t)F; change_adv = adv;
            change_base = base_sum / base_n; ticks_at_change = afx_status_timer_ticks();
            break;
        }
        if (polls > 4) { base_sum += adv; base_n++; if (base_n > 64) { base_sum -= base_sum / base_n; base_n--; } }
    }
    if (F_change_now < 0) { printf("FLOWTIME_FAIL pitch change never seen\n"); goto out; }

    /* The change happened somewhere between the two polls; take the midpoint. */
    double F_mid = ((double)F_change_prev + (double)F_change_now) / 2.0, F_half = ((double)F_change_now - (double)F_change_prev) / 2.0;
    double hz = (double)wait_ticks * 44100.0 / F_mid;
    printf("FLOWTIME_RATE_CHANGE polls=%lu F_prev=%lld F_now=%lld adv=%lu baseline_adv=%.1f ticks_since_start=%lu\n",
           (unsigned long)polls, (long long)F_change_prev, (long long)F_change_now, (unsigned long)change_adv,
           change_base, (unsigned long)(ticks_at_change - ticks_start));
    printf("FLOWTIME_RESULT authored_seconds=%lu authored_ticks=%lu measured_frames=%.0f (+-%.0f) expected_frames=%.0f "
           "err=%+.4f%% implied_tick_hz=%.3f frames_per_tick=%.4f\n",
           (unsigned long)seconds, (unsigned long)wait_ticks, F_mid, F_half, seconds * 44100.0,
           100.0 * (F_mid - seconds * 44100.0) / (seconds * 44100.0), hz, F_mid / wait_ticks);
    *out_hz = hz; *out_frames = (int64_t)F_mid;
    ok = 1;
out:
    r = release_voice(inst, flow);
    if (r) { printf("release FAIL (%d)\n", r); ok = 0; }
    return ok;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    alignas(32) static const unsigned char firmware[] = {
#embed "../../../firmware/aicaflow.drv"
    };
    afx_bank_t bank = {0};
    int result = 0, all_ok = 1;

    printf("FLOWTIME_BEGIN\n");
    result = afx_init(firmware, sizeof(firmware));
    if (!result) result = load_bank(&bank);
    if (result) { printf("setup FAIL (%d)\n", result); all_ok = 0; goto done; }

    static const uint32_t waits[] = {10, 20};          /* authored seconds */
    double hz[2] = {0, 0}; int64_t fr[2] = {0, 0};
    for (int i = 0; i < 2; i++) {
        if (!measure(&bank, waits[i], &hz[i], &fr[i])) { all_ok = 0; break; }
    }
    if (all_ok) {
        /* PASS: authored time agrees with AICA audio time within 0.05% (the residual 0.02-0.03% is the
         * ~0.01 sample FIQ reload latency per 44-sample tick).  The old 1000 Hz behaviour was -0.2%. */
        for (int i = 0; i < 2; i++) {
            double expect = waits[i] * 44100.0, f = (double)fr[i], err = 100.0 * (f - expect) / expect;
            int pass = err > -0.05 && err < 0.05;
            const char *why = pass ? "authored time matches AICA time"
                            : (err < -0.15 && err > -0.30) ? "STILL ~0.2% fast: tick rate not applied (1000 Hz assumption)"
                                                           : "UNEXPECTED timing";
            printf("FLOWTIME_VERDICT authored_seconds=%lu measured_frames=%lld expected_frames=%.0f err=%+.4f%% -> %s: %s\n",
                   (unsigned long)waits[i], (long long)fr[i], expect, err, pass ? "PASS" : "FAIL", why);
            if (!pass) all_ok = 0;
        }
    }
done:
    afx_shutdown();
    printf("%s\nFLOWTIME_END\n", all_ok ? "FLOWTIME_DONE" : "FLOWTIME_FAIL");
    return all_ok ? 0 : 1;
}
