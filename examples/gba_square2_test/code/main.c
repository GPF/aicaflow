/*
 * gba_square2_test — Standalone AICAflow experiment reproducing GBA Square 2
 * channel behaviour on one AICA hardware voice.
 *
 * Tests:
 *   Phase 1 — Pitch:  freq_reg 0 / 857 / 1536
 *   Phase 2 — Duty:   all four GBA duty patterns
 *   Phase 3 — Static level:  0 / 7 / 15
 *   Phase 4 — Runtime level ramp: 15 -> 14 -> ... -> 0, one step per 1/64 s (GBA envelope tick)
 *
 * GBA Square 2 frequency formula (verified in mGBADC/src/gb/audio.c):
 *   freq = 524288 / (2048 - frequency_register) Hz
 *
 * AICAflow pitch word format (from authoring docs):
 *   bits 11-14: signed octave (-8..+7)
 *   bits 0-9:   10-bit FNS fraction
 *   ratio = 2^octave × (1 + FNS/1024)
 *   Output frequency = 44100/8 × ratio = 5512.5 × ratio   (8-sample loop)
 *
 * PCM16 waveform: +32767 / -32767 (no +32768 in signed 16-bit).
 * Duty values from mGBADC/src/gb/audio.c _squareChannelDuty[4][8]:
 *   pattern 0: { 0,0,0,0,0,0,0,1 }
 *   pattern 1: { 1,0,0,0,0,0,0,1 }
 *   pattern 2: { 1,0,0,0,0,1,1,1 }
 *   pattern 3: { 0,1,1,1,1,1,1,0 }
 */

#include <kos.h>

#include <aicaflow/bank.h>
#include <aicaflow/host.h>
#include <aicaflow/codec.h>

#include <stdio.h>
#include <stdalign.h>
#include <string.h>

/* ── GBA Square 2 duty table (from mGBADC/src/gb/audio.c) ─────────────── */

static const int gba_duty[4][8] = {
    { 0, 0, 0, 0, 0, 0, 0, 1 },   /* pattern 0: 1/8 duty */
    { 1, 0, 0, 0, 0, 0, 0, 1 },   /* pattern 1: 1/4 duty */
    { 1, 0, 0, 0, 0, 1, 1, 1 },   /* pattern 2: 1/2 duty */
    { 0, 1, 1, 1, 1, 1, 1, 0 },   /* pattern 3: 3/4 duty */
};

/* ── GBA frequency formula ──────────────────────────────────────────────── */

static uint32_t gba_freq(uint16_t reg) {
    return 524288u / (2048u - reg);
}

/* ── AICAflow pitch word: signed octave (bits 11-14) + 10-bit FNS (bits 0-9)
 *    ratio = 2^octave × (1 + FNS/1024)
 *    For an 8-sample loop at 44.1 kHz, unpitched cycle rate = 5512.5 Hz.
 *    pitch = (octave & 0xF)<<11 | FNS */

static uint16_t pitch_for_hz(float target_hz) {
    float ratio = target_hz / 5512.5f;
    /* Find signed octave in [-8..7] that minimises error */
    int octave = 0;
    float r = ratio;
    while (r >= 2.0f && octave < 7) { r /= 2.0f; octave++; }
    while (r < 1.0f && octave > -8) { r *= 2.0f; octave--; }
    /* Now 1.0 <= r < 2.0; FNS = (r-1)*1024 rounded */
    uint16_t fns = (uint16_t)((r - 1.0f) * 1024.0f + 0.5f);
    if (fns > 1023u) { fns = 0; if (octave < 7) octave++; else fns = 1023u; }
    /* Bits 11-14 are a 4-bit signed field; bits 0-9 are FNS. */
    return (uint16_t)(((octave & 0xF) << 11) | fns);
}

/* ── Bank: 4 duty waveforms, 32-byte slots, 8 frames + 4 wrap guard frames ── */

enum {
    WAVE_FRAMES = 8, GUARD_FRAMES = 4,
    WAVE_BYTES = (WAVE_FRAMES + GUARD_FRAMES) * 2,   /* 24 */
    WAVE_SLOT = 32,
    BANK_BYTES = 4 * WAVE_SLOT,
    BANK_ID_LOW = 0x32515347u                        /* "GSQ2" */
};

alignas(32) static uint8_t bank_file[AFX_BANK_HEADER_BYTES + BANK_BYTES];

static int load_duty_bank(afx_bank_t *bank) {
    uint8_t *file = bank_file;
    memset(file, 0, sizeof(bank_file));
    afx_write32(file, AFX_BANK_MAGIC); afx_write32(file + 4, AFX_BANK_VERSION);
    afx_write32(file + 8, BANK_ID_LOW); afx_write32(file + 12, 0);
    afx_write32(file + 16, AFX_BANK_HEADER_BYTES); afx_write32(file + 20, BANK_BYTES);
    afx_write32(file + 24, sizeof(bank_file));
    for (int pat = 0; pat < 4; pat++) {
        uint8_t *slot = file + AFX_BANK_HEADER_BYTES + pat * WAVE_SLOT;
        for (int i = 0; i < WAVE_FRAMES + GUARD_FRAMES; i++) {
            int16_t s = gba_duty[pat][i % WAVE_FRAMES] ? 32767 : -32767;
            slot[i * 2] = (uint8_t)(s & 0xff);
            slot[i * 2 + 1] = (uint8_t)((uint16_t)s >> 8);
        }
    }
    return afx_bank_load_memory(bank, file, sizeof(bank_file));
}

/* MIX high byte is TL attenuation (0 = loud, 0xff = mute); low byte 0x24 =
 * open filter, as in dynamic_sfx.  GBA volume 0..15 -> attenuation. */
static uint16_t mix_for_level(uint8_t level) {
    uint16_t att = level ? (uint16_t)((15u - level) * 2u) : 0xffu;
    return (uint16_t)(att << 8 | 0x24);
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

/* One looping voice at `pitch`/`level`, held with PARK until stopped. */
static int build_flow(const afx_bank_t *bank, int pattern, uint16_t pitch,
                      uint8_t level, afx_asset_t *out_flow) {
    uint16_t fields[AFX_FIELD_COUNT] = {0};
    uint32_t offset = (uint32_t)pattern * WAVE_SLOT;
    fields[AFX_FIELD_CONTROL] = (uint16_t)(0x0200u | (offset >> 16));
    fields[AFX_FIELD_SAMPLE_LOW] = (uint16_t)offset;
    fields[AFX_FIELD_LOOP_END] = WAVE_FRAMES + GUARD_FRAMES - 5;  /* = 7, as dynamic_sfx */
    fields[AFX_FIELD_ENV_AD] = 0x001f;
    fields[AFX_FIELD_ENV_DR] = 0x001f;
    fields[AFX_FIELD_DIRECT] = 0x0f00u | 15u;     /* centre pan */
    fields[AFX_FIELD_MIX] = 0x0024;
    for (unsigned f = AFX_FIELD_FILTER_LEVEL0; f <= AFX_FIELD_FILTER_LEVEL4; ++f)
        fields[f] = 0x1fff;

    uint8_t stream[9];
    uint32_t written = 0, bytes;
    uint16_t note[] = {pitch, mix_for_level(level)};
    afx_event_t event = {.opcode = AFX_OP_NOTE_PL, .channel = 0, .setup = 0,
                         .mask = AFX_NOTE_PL_MASK};
    if (afx_encode_event(stream, sizeof(stream), &event, note, &bytes)) return -1;
    written = bytes;
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
    afx_write32(file + 80, 0); afx_write32(file + 84, offset); afx_write32(file + 88, WAVE_BYTES);
    for (unsigned f = 0; f < AFX_FIELD_COUNT; ++f)
        afx_write16(file + 96 + f * 2u, fields[f]);
    memcpy(file + 96 + AFX_SETUP_BYTES, stream, written);
    afx_write32(file + 32, afx_control_id(file + 96, image_bytes));
    return afx_bank_flow_upload(bank, file, 96 + image_bytes, out_flow);
}

/* stop -> DONE -> recycle -> handle stale -> free asset.  Without the recycle
 * the instance keeps its channel, exec budget and asset reference (-10 after
 * ~9 plays). */
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
        if (r) printf("  release FAIL (%d)\n", r);
    }
    if (!r && f) { int fr = afx_asset_free(f); if (fr) { printf("  asset_free FAIL (%d)\n", fr); r = fr; } }
    return r;
}

/* Build, start, wait for PARKED, hold `ms`, stop. */
static int play(const afx_bank_t *bank, int pattern, uint16_t pitch, uint8_t level, uint32_t ms) {
    afx_asset_t f = 0;
    afx_instance_t inst = 0;
    int r = build_flow(bank, pattern, pitch, level, &f);
    if (r) { printf("  build_flow FAIL (%d)\n", r); return r; }
    r = afx_instance_activate(f, &inst);
    if (!r) r = wait_for(inst, AFX_PARKED, 1000);
    if (r) { printf("  activate/park FAIL (%d)\n", r); }
    else thd_sleep(ms);
    int rr = release_voice(inst, f);
    return r ? r : rr;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    alignas(32) static const unsigned char firmware[] = {
#embed "../../../firmware/aicaflow.drv"
    };

    afx_bank_t bank = {0};
    int result = 0;

    printf("GBA_SQ2_TEST_BEGIN\n");
    result = afx_init(firmware, sizeof(firmware));
    if (result) { printf("afx_init FAIL (%d)\n", result); goto done; }
    result = load_duty_bank(&bank);
    if (result) { printf("bank load FAIL (%d)\n", result); goto done; }

    /* Phase 1 — Pitch (pattern 2 = 50%) */
    printf("PHASE=PITCH\n");
    uint16_t freq_regs[] = { 0, 857, 1536 };
    for (unsigned i = 0; i < 3 && !result; i++) {
        uint32_t hz = gba_freq(freq_regs[i]);
        uint16_t pitch = pitch_for_hz((float)hz);
        printf("PITCH freq_reg=%u target_hz=%lu aica_pitch=0x%04x\n",
               freq_regs[i], (unsigned long)hz, pitch);
        result = play(&bank, 2, pitch, 15, 2000);
    }

    /* Phase 2 — Duty */
    printf("PHASE=DUTY\n");
    for (int pat = 0; pat < 4 && !result; pat++) {
        printf("DUTY index=%d\n", pat);
        result = play(&bank, pat, pitch_for_hz(440.2f), 7, 2000);
    }

    /* Phase 3 — Static level */
    printf("PHASE=LEVEL\n");
    uint8_t levels[] = { 0, 7, 15 };
    for (int li = 0; li < 3 && !result; li++) {
        printf("LEVEL requested=%u mix=0x%04x\n", levels[li], mix_for_level(levels[li]));
        result = play(&bank, 2, pitch_for_hz(440.2f), levels[li], 1500);
    }

    /* Phase 4 — Runtime level ramp via afx_instance_patch(MIX) */
    printf("PHASE=RAMP\n");
    if (!result) {
        afx_asset_t f = 0;
        afx_instance_t inst = 0;
        result = build_flow(&bank, 2, pitch_for_hz(440.2f), 15, &f);
        if (!result) result = afx_instance_activate(f, &inst);
        if (!result) result = wait_for(inst, AFX_PARKED, 1000);
        /* GBA envelope step = 1/64 s (period 1).  Deadlines are absolute so
         * sleep jitter does not accumulate; log requested vs actual submit. */
        uint64_t t0 = timer_us_gettime64();
        for (int step = 0; step < 16 && !result; step++) {
            uint64_t due = t0 + (uint64_t)step * 1000000u / 64u;
            /* thd_sleep(1) is a ~10 ms scheduler tick, so only sleep when the
             * deadline is well past one tick away; busy-wait the remainder. */
            for (uint64_t now; (now = timer_us_gettime64()) < due; ) {
                if (due - now > 12000) thd_sleep(1);
            }
            uint8_t level = (uint8_t)(15 - step);
            uint16_t mix = mix_for_level(level);
            uint64_t sent = timer_us_gettime64();
            int pr = afx_instance_patch(inst, 0, 1u << AFX_FIELD_MIX, &mix);
            afx_update();
            afx_instance_status_t st = {0};
            afx_instance_status(inst, &st);
            printf("  PATCH level=%u mix=0x%04x due_us=%llu sent_us=%llu late_us=%lld submit=%s state=%lu\n",
                   level, mix, (unsigned long long)(due - t0), (unsigned long long)(sent - t0),
                   (long long)(sent - due), pr ? "FAIL" : "OK", (unsigned long)st.state);
            if (pr && pr != -AFX_BUSY && pr != -AFX_IPC_FULL) { result = pr; break; }
        }
        { int rr = release_voice(inst, f); if (!result) result = rr; }
    }

done:
    if (result == 0) printf("GBA_SQ2_TEST_READY_FOR_CAPTURE\n");
    else printf("GBA_SQ2_TEST_FAIL (%d)\n", result);

    afx_shutdown();
    printf("Aicaflow gba_square2_test: %s (%d)\n", result ? "FAIL" : "PASS", result);
    return result ? 1 : 0;
}
