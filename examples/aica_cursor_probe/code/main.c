/*
 * aica_cursor_probe — can the SH-4 read the AICA playback cursor of an
 * AICAflow-owned looping voice while the normal AICAflow ARM7 firmware runs?
 *
 * Diagnostic only: no PCM ring, no refill, no new ARM7 logic, no snd_stream.
 *
 * Register mechanism (from KOS kernel/arch/dreamcast/sound/arm/aica.c,
 * aica_get_pos()):
 *     SNDREG8(0x280d) = ch;            select channel (monitor select, global)
 *     short nop delay
 *     pos = SNDREG32(0x2814) & 0xffff; sample index of that channel
 * The ARM sees AICA registers at 0x00800000; the SH-4 sees them at 0xa0700000
 * (AFX_AICA_REG_BASE 0x800000 on ARM, dsp_scene.c uses 0xa0702000 on SH-4).
 * Measured on hardware: from the SH-4 the *byte* write is unreliable; a 32-bit
 * read-modify-write of the word at 0x280c (channel in bits 8..13) selects
 * correctly, after a settle of about one AICA sample period (~22.7 us).
 * A startup scan re-verifies this and picks the working method.
 *
 * The physical channel is read from AICAflow's own channel-map arena in SPU
 * RAM (AFX_CHANNEL_MAP_ARENA_ADDR): with one voice on a fresh AICAflow, the
 * first instance occupies arena 0, entry 0, which holds its physical channel.
 */

#include <kos.h>

#include <aicaflow/bank.h>
#include <aicaflow/host.h>
#include <aicaflow/codec.h>

#include <math.h>
#include <stdio.h>
#include <stdalign.h>
#include <string.h>

/* KOS timer_us_gettime64() runs 0.25% slow and steps +2.5 ms once per second
 * (TMU2 ticks are 80.2 ns, not 80 ns; see docs/results/aica-ring-mode-a.md). */
#define TMU2_TICKS_PER_SEC 12468720.0
static inline uint64_t now_us(void) {
    timer_val_t v = __dreamcast_get_ticks();
    return (uint64_t)v.secs * 1000000u + (uint64_t)((double)v.ticks * (1000000.0 / TMU2_TICKS_PER_SEC));
}

#define AICA_REG_SH4        0xa0700000u
#define MON_SELECT_ADDR     (AICA_REG_SH4 + 0x280du)
#define MON_POS_ADDR        (AICA_REG_SH4 + 0x2814u)
#define SPU_RAM_SH4         0xa0800000u

enum {
    WAVE_FRAMES = 4096,                  /* loop length: 4096 / 44100 = 92.9 ms */
    GUARD_FRAMES = 4,
    WAVE_BYTES = (WAVE_FRAMES + GUARD_FRAMES) * 2,
    BANK_BYTES = (WAVE_BYTES + 31) & ~31,
    BANK_ID_LOW = 0x55435041u,           /* "APCU" */
    PITCH_NATIVE = 0x0000,               /* octave 0, FNS 0 = 44100 Hz */
    PITCH_2X = 0x0800,                   /* octave +1 = 88200 Hz */
    MIX_MUTE = 0xff24,
    POLL_MS = 10,
    RUN_MS = 10000,
    PRINT_EVERY = 5,
    MIX_LOUD = 0x0c24, MIX_QUIET = 0x1424
};

alignas(32) static uint8_t bank_file[AFX_BANK_HEADER_BYTES + BANK_BYTES];

static int load_bank(afx_bank_t *bank) {
    memset(bank_file, 0, sizeof(bank_file));
    afx_write32(bank_file, AFX_BANK_MAGIC); afx_write32(bank_file + 4, AFX_BANK_VERSION);
    afx_write32(bank_file + 8, BANK_ID_LOW); afx_write32(bank_file + 12, 0);
    afx_write32(bank_file + 16, AFX_BANK_HEADER_BYTES); afx_write32(bank_file + 20, BANK_BYTES);
    afx_write32(bank_file + 24, sizeof(bank_file));
    uint8_t *pcm = bank_file + AFX_BANK_HEADER_BYTES;
    for (int i = 0; i < WAVE_FRAMES + GUARD_FRAMES; i++) {
        int16_t s = (int16_t)(12000.0f * sinf(6.2831853f * 16.0f * (float)(i % WAVE_FRAMES) / WAVE_FRAMES));
        pcm[i * 2] = (uint8_t)(s & 0xff);
        pcm[i * 2 + 1] = (uint8_t)((uint16_t)s >> 8);
    }
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

/* One looping voice, held with PARK; same flow layout as dynamic_sfx. */
static int build_flow(const afx_bank_t *bank, uint16_t pitch, uint16_t mix, afx_asset_t *out_flow) {
    uint16_t fields[AFX_FIELD_COUNT] = {0};
    fields[AFX_FIELD_CONTROL] = 0x0200u;               /* loop, bank offset 0 */
    fields[AFX_FIELD_SAMPLE_LOW] = 0;
    fields[AFX_FIELD_LOOP_END] = WAVE_FRAMES - 1;      /* frames + guards - 5 */
    fields[AFX_FIELD_ENV_AD] = 0x001f;
    fields[AFX_FIELD_ENV_DR] = 0x001f;
    fields[AFX_FIELD_DIRECT] = 0x0f00u;           /* pan 0 = centre (15 is a hard pan) */
    fields[AFX_FIELD_MIX] = 0x0024;
    for (unsigned f = AFX_FIELD_FILTER_LEVEL0; f <= AFX_FIELD_FILTER_LEVEL4; ++f)
        fields[f] = 0x1fff;

    uint8_t stream[9];
    uint32_t bytes;
    uint16_t note[] = {pitch, mix};
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
    afx_write32(file + 80, 0); afx_write32(file + 84, 0); afx_write32(file + 88, WAVE_BYTES);
    for (unsigned f = 0; f < AFX_FIELD_COUNT; ++f)
        afx_write16(file + 96 + f * 2u, fields[f]);
    memcpy(file + 96 + AFX_SETUP_BYTES, stream, written);
    afx_write32(file + 32, afx_control_id(file + 96, image_bytes));
    return afx_bank_flow_upload(bank, file, 96 + image_bytes, out_flow);
}

/* stop -> DONE -> recycle -> handle stale -> free asset. */
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

/* Select a hardware channel in the AICA monitor and read its sample index.
 * The select method and settle delay are runtime variables so the scan below can
 * compare them on real hardware.
 *   SEL_BYTE : g2_write_8 to 0x280d, exactly what KOS's ARM driver does.
 *   SEL_W16  : read-modify-write of the 16-bit word at 0x280c (bits 8..13 = channel).
 *   SEL_W32  : read-modify-write of the 32-bit word at 0x280c.                    */
enum { SEL_BYTE, SEL_W16, SEL_W32, SEL_MODES };
static const char *const sel_name[SEL_MODES] = {"byte", "w16", "w32"};
/* Hardware-measured default (see docs/results/aica-cursor-probe.md): the byte
 * write KOS's ARM driver uses does NOT select reliably from the SH-4; the 32-bit
 * read-modify-write does, and the monitor needs >= ~25 us (one sample period). */
static int g_sel_mode = SEL_W32;
static unsigned g_sel_delay_us = 50;

static void select_channel(int mode, unsigned channel) {
    switch (mode) {
    case SEL_W16: {
        uint16_t w = g2_read_16(AICA_REG_SH4 + 0x280cu);
        g2_write_16(AICA_REG_SH4 + 0x280cu, (uint16_t)((w & ~0xff00u) | (channel << 8)));
        break;
    }
    case SEL_W32: {
        uint32_t w = g2_read_32(AICA_REG_SH4 + 0x280cu);
        g2_write_32(AICA_REG_SH4 + 0x280cu, (w & ~0xff00u) | ((uint32_t)channel << 8));
        break;
    }
    default:
        g2_write_8(MON_SELECT_ADDR, (uint8_t)channel);
    }
}

static uint32_t cursor_read_with(int mode, unsigned delay_us, unsigned channel) {
    g2_ctx_t ctx = g2_lock();
    select_channel(mode, channel);
    timer_spin_delay_us((unsigned short)delay_us);
    uint32_t pos = g2_read_32(MON_POS_ADDR) & 0xffffu;
    g2_unlock(ctx);
    return pos;
}

static uint32_t cursor_read(unsigned channel) {
    return cursor_read_with(g_sel_mode, g_sel_delay_us, channel);
}

/* Measured rate (samples/ms) of what the monitor reports after selecting `channel`. */
static double scan_rate(int mode, unsigned delay_us, unsigned channel) {
    uint64_t ta = now_us();
    uint32_t a = cursor_read_with(mode, delay_us, channel);
    timer_spin_delay_us(4000);
    uint64_t tb = now_us();
    uint32_t b = cursor_read_with(mode, delay_us, channel);
    uint32_t d = b >= a ? b - a : b + WAVE_FRAMES - a;
    return (double)d * 1000.0 / (double)(tb - ta);
}

#define CHECK(name, ok) do { printf("CHECK %s: %s\n", name, (ok) ? "PASS" : "FAIL"); \
                             if (!(ok)) all_ok = 0; } while (0)

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    alignas(32) static const unsigned char firmware[] = {
#embed "../../../firmware/aicaflow.drv"
    };

    afx_bank_t bank = {0};
    afx_asset_t flow = 0, ref_flow = 0;
    afx_instance_t inst = 0, ref_inst = 0;
    int result = 0, all_ok = 1;

    printf("AICA_CURSOR_PROBE_BEGIN\n");
    result = afx_init(firmware, sizeof(firmware));
    if (!result) result = load_bank(&bank);
    /* Reference voice first: takes channel 0 (the monitor's power-on default
     * selection), muted, at 2x rate.  The probe voice then lands on channel 1,
     * so a monitor that ignores the select write would read the wrong rate. */
    if (!result) result = build_flow(&bank, PITCH_2X, MIX_MUTE, &ref_flow);
    if (!result) result = afx_instance_activate(ref_flow, &ref_inst);
    if (!result) result = wait_for(ref_inst, AFX_PARKED, 1000);
    if (!result) result = build_flow(&bank, PITCH_NATIVE, MIX_LOUD, &flow);
    if (!result) result = afx_instance_activate(flow, &inst);
    if (!result) result = wait_for(inst, AFX_PARKED, 1000);
    if (result) { printf("setup FAIL (%d)\n", result); goto done; }

    /* Physical channel: arena 0 / entry 0 of AICAflow's channel map. */
    uint32_t map[4];
    {
        g2_ctx_t ctx = g2_lock();
        for (int i = 0; i < 4; i++)
            map[i] = g2_read_32(SPU_RAM_SH4 + AFX_CHANNEL_MAP_ARENA_ADDR + i * 4);
        g2_unlock(ctx);
    }
    unsigned ch = map[1];                 /* probe voice: second instance, arena 0 entry 1 */
    unsigned ctl = map[0];                /* reference voice at 2x rate */
    if (ch >= AFX_AICA_CHANNEL_COUNT || ctl >= AFX_AICA_CHANNEL_COUNT || ch == ctl) {
        printf("channel map invalid: ref=%lu probe=%lu\n", (unsigned long)map[0], (unsigned long)map[1]);
        result = -1; goto done;
    }
    printf("SETUP ch=%u map[0..3]=%lu,%lu,%lu,%lu ref_ch=%u (2x rate, muted)\n", ch,
           (unsigned long)map[0], (unsigned long)map[1], (unsigned long)map[2],
           (unsigned long)map[3], ctl);
    printf("SETUP sample_base=0x%08lx loop_start=0 loop_end=%d frames=%d rate_hz=44100 "
           "expect_samples_per_ms=44.1 loop_ms=%.1f\n",
           (unsigned long)afx_asset_addr(bank.asset), WAVE_FRAMES - 1, WAVE_FRAMES,
           WAVE_FRAMES * 1000.0 / 44100.0);
    printf("SETUP monitor_select=0x%08lx monitor_pos=0x%08lx word_280c=0x%08lx\n",
           (unsigned long)MON_SELECT_ADDR, (unsigned long)MON_POS_ADDR,
           (unsigned long)g2_read_32(AICA_REG_SH4 + 0x280cu));


    /* Settle trace: park the monitor on idle ch3 (constant), then select ch0 (playing)
     * and read back-to-back.  The point where the value jumps is the select latency. */
    for (int m = 0; m < SEL_MODES; m++) {
        uint32_t v[14]; uint32_t at[14];
        g2_ctx_t ctx = g2_lock();
        select_channel(m, 3);
        timer_spin_delay_us(2000);
        uint64_t ts = now_us();
        select_channel(m, 0);
        for (int k = 0; k < 14; k++) {
            v[k] = g2_read_32(MON_POS_ADDR) & 0xffffu;
            at[k] = (uint32_t)(now_us() - ts);
            timer_spin_delay_us(4);
        }
        g2_unlock(ctx);
        printf("TRACE mode=%s sel 3->0:", sel_name[m]);
        for (int k = 0; k < 14; k++) printf(" %luus=%lu", (unsigned long)at[k], (unsigned long)v[k]);
        printf("\n");
    }

    /* Selector scan: voices are ch0 (88.2 samples/ms) and ch1 (44.1); every other
     * channel is idle.  A working select shows sel0 -> ~88, sel1 -> ~44, rest ~0.
     * Three trials per cell; the monitor is parked on idle ch3 between cells. */
    {
        static const unsigned sels[] = {0, 1, 2, 3, 33};
        static const unsigned delays[] = {50, 100, 500};
        int best_mode = -1; unsigned best_delay = 0;
        for (int m = 0; m < SEL_MODES; m++)
            for (unsigned di = 0; di < 3; di++) {
                double r[5][3]; int ok = 1;
                printf("SCAN mode=%s delay_us=%u", sel_name[m], delays[di]);
                for (unsigned i = 0; i < 5; i++) {
                    printf(" sel%u=", sels[i]);
                    for (int t = 0; t < 3; t++) {
                        select_channel(m, 3); timer_spin_delay_us(1000);
                        r[i][t] = scan_rate(m, delays[di], sels[i]);
                        printf("%s%.1f", t ? "/" : "", r[i][t]);
                    }
                }
                printf("\n");
                for (int t = 0; t < 3; t++)
                    ok &= r[0][t] > 88.2 * 0.95 && r[0][t] < 88.2 * 1.05 &&
                          r[1][t] > 44.1 * 0.95 && r[1][t] < 44.1 * 1.05 &&
                          r[2][t] < 2.0 && r[3][t] < 2.0 && r[4][t] < 2.0;
                if (ok && best_mode < 0) { best_mode = m; best_delay = delays[di]; }
            }
        if (best_mode >= 0) {
            g_sel_mode = best_mode; g_sel_delay_us = best_delay;
            printf("SCAN_BEST mode=%s delay_us=%u\n", sel_name[best_mode], best_delay);
        } else printf("SCAN_BEST none (no mode selects channels correctly)\n");
    }

    uint64_t t0 = now_us(), t_prev = t0;
    uint32_t prev = cursor_read(ch);
    uint32_t polls = 0, advancing = 0, run_const = 0, max_const = 0, wraps = 0, oor = 0,
             max_pos = prev, skipped = 0, ctl_same = 0, ctl_changes = 0, bad_state = 0,
             patches = 0, patch_fail = 0, skip_dt = 0;
    uint64_t sum_samples = 0, sum_us = 0, ctl_samples = 0, ctl_us = 0;
    uint32_t ctl_prev = cursor_read(ctl);
    uint16_t mix_now = MIX_LOUD;

    printf("CURSOR t_ms=0 ch=%u pos=%lu delta=0\n", ch, (unsigned long)prev);
    while ((now_us() - t0) < (uint64_t)RUN_MS * 1000u) {
        thd_sleep(POLL_MS);
        afx_update();
        afx_instance_status_t st = {0};
        if (afx_instance_status(inst, &st) || st.state != AFX_PARKED) bad_state++;

        uint64_t now = now_us();
        uint32_t pos = cursor_read(ch);
        uint32_t cpos = cursor_read(ctl);
        uint64_t dt = now - t_prev;
        polls++;

        if (pos >= WAVE_FRAMES) { oor++; printf("CURSOR_RANGE pos=%lu >= %d\n", (unsigned long)pos, WAVE_FRAMES); }
        if (pos > max_pos) max_pos = pos;
        if (cpos == pos) ctl_same++;
        if (cpos != ctl_prev) ctl_changes++;
        uint32_t cdelta = cpos >= ctl_prev ? cpos - ctl_prev : cpos + WAVE_FRAMES - ctl_prev;
        ctl_prev = cpos;

        uint32_t delta = pos >= prev ? pos - prev : pos + WAVE_FRAMES - prev;
        if (pos < prev) { wraps++; printf("CURSOR_WRAP prev=%lu pos=%lu\n", (unsigned long)prev, (unsigned long)pos); }
        if (pos == prev) { if (++run_const > max_const) max_const = run_const; } else { run_const = 0; advancing++; }

        if (dt < 40000u) { sum_samples += delta; sum_us += dt; ctl_samples += cdelta; ctl_us += dt; }   /* < one loop: unambiguous */
        else { skipped++; skip_dt++; }

        if (polls % PRINT_EVERY == 0)
            printf("CURSOR t_ms=%llu ch=%u pos=%lu delta=%lu ctl_pos=%lu\n",
                   (unsigned long long)((now - t0) / 1000u), ch, (unsigned long)pos,
                   (unsigned long)delta, (unsigned long)cpos);

        /* Ordinary AICAflow traffic while the monitor is being polled. */
        if (polls % 10 == 0) {
            mix_now = (mix_now == MIX_LOUD) ? MIX_QUIET : MIX_LOUD;
            int pr = afx_instance_patch(inst, 0, 1u << AFX_FIELD_MIX, &mix_now);
            if (pr) patch_fail++; else patches++;
        }
        prev = pos;
        t_prev = now;
    }

    double ctl_rate = ctl_us ? (double)ctl_samples * 1000.0 / (double)ctl_us : 0.0;
    double rate = sum_us ? (double)sum_samples * 1000.0 / (double)sum_us : 0.0;   /* samples/ms */
    printf("SUMMARY polls=%lu advancing=%lu max_const_run=%lu wraps=%lu out_of_range=%lu max_pos=%lu "
           "skipped_gaps=%lu rate=%.3f samples/ms (expect 44.100) ctl_rate=%.3f samples/ms (expect 88.200) ctl_same_as_voice=%lu ctl_changes=%lu "
           "bad_state=%lu patches=%lu patch_fail=%lu\n",
           (unsigned long)polls, (unsigned long)advancing, (unsigned long)max_const,
           (unsigned long)wraps, (unsigned long)oor, (unsigned long)max_pos,
           (unsigned long)skipped, rate, ctl_rate, (unsigned long)ctl_same, (unsigned long)ctl_changes,
           (unsigned long)bad_state, (unsigned long)patches, (unsigned long)patch_fail);
    (void)skip_dt;

    CHECK("advancing", polls > 100 && advancing * 10 > polls * 9 && max_const < 3);
    CHECK("range", oor == 0);
    CHECK("wrap", wraps >= 10);
    CHECK("rate_within_3pct", rate > 44.1 * 0.97 && rate < 44.1 * 1.03);
    CHECK("select_works_ref_rate_2x", ctl_rate > 88.2 * 0.97 && ctl_rate < 88.2 * 1.03);
    CHECK("select_distinguishes_channels", ctl_same * 10 < polls);
    CHECK("playback_stable", bad_state == 0 && patch_fail == 0);

done:
    {
        int rr = release_voice(inst, flow);
        int rr2 = release_voice(ref_inst, ref_flow);
        if (!rr) rr = rr2;
        if (rr) { printf("release FAIL (%d)\n", rr); all_ok = 0; }
        if (!result) result = rr;
    }
    afx_shutdown();
    if (!result && all_ok) printf("CURSOR PROBE READY FOR PCM REFILL EXPERIMENT\n");
    else printf("AICA_CURSOR_PROBE_FAIL (result=%d)\n", result);
    printf("AICA_CURSOR_PROBE_END\n");
    return (!result && all_ok) ? 0 : 1;
}
