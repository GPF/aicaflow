#define PLAYER_TITLE "AICAFLOW CLASSICAL PLAYER"
#define PLAYER_MODE_NAME "AICAflow Classical Music"
#define PLAYER_EXPECTED_SONGS 3
#define PLAYER_SELFTEST_CASES {0}
#define PLAYER_SELFTEST_EXIT_SONG 0
#define PLAYER_SONG_BANK_FILE(index) songs[(index)].bank
#define PLAYER_SONG_PROFILE(index) (songs[(index)].dsp ? songs[(index)].dsp : "dry")
#define PLAYER_DSP_PROGRAM(name, program) afx_dsp_program_preset((program), (name))
#define PLAYER_LIST_HEADING "AFB + AFX KiB"
#include "../player.c"
