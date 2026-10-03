/* Exercise every generated AFB/AFX/AFC/AFV pair through the production
   asynchronous loader and real enDjinn frame loop. */
#include <assert.h>

static enj_ctrlr_state_t *frame_test_input(void) {
    static enj_ctrlr_state_t pad;
    static unsigned song, phase;
    static bool saw_bank_progress[SONG_COUNT], saw_visual_progress[SONG_COUNT];
    memset(&pad, 0, sizeof(pad));
    if (bank_loader.file) {
        assert(loading_label && loading_total && loading_done <= loading_total);
        if (loading_done) saw_bank_progress[song] = true;
    }
    if (visual_file) {
        assert(visual_loading_size && visual_loading_bytes <= visual_loading_size);
        if (visual_loading_bytes) saw_visual_progress[song] = true;
    }
    if (phase == 0 && playing == (int)song && playback_ms() >= 1500) {
        assert(saw_bank_progress[song] && saw_visual_progress[song]);
        assert(visual_frames && spectrum_frame != UINT32_MAX);
        pad.button.B = ENJ_BUTTON_DOWN_THIS_FRAME;
        phase = 1;
    } else if (phase == 1 && playing == -1) {
        if (++song == SONG_COUNT) {
            printf("CLASSICAL PLAYER FRAME TEST PASS: AFB and AFV progress, all AFX/AFC pairs\n");
            enj_state_flag_shutdown(NULL);
        } else {
            pad.button.DOWN = ENJ_BUTTON_DOWN_THIS_FRAME;
            phase = 2;
        }
    } else if (phase == 2 && selected == (int)song) {
        pad.button.A = ENJ_BUTTON_DOWN_THIS_FRAME;
        phase = 0;
    }
    return &pad;
}
