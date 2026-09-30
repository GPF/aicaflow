#ifndef AFX_HARDWARE_CHECK_TERMINAL_H
#define AFX_HARDWARE_CHECK_TERMINAL_H

#include <stdint.h>

#define TERMINAL_WIDTH 79
#define TERMINAL_LINES_PR_SCREEN 30
#define TERMINAL_LINES_CAPACITY 1024
#define TEXT_TMARGIN_LEFT 10

/* Recovered from previous_attempts/afx2. */
typedef struct {
    char buffer[TERMINAL_LINES_CAPACITY][TERMINAL_WIDTH + 1];
    uint8_t line_lengths[TERMINAL_LINES_CAPACITY];
    uint8_t cur_line;
    uint8_t cur_col;
    int32_t auto_scroll;
    int32_t user_scroll;
} terminal_buffer_t;

void terminal_clear(terminal_buffer_t *term);
void terminal_write(terminal_buffer_t *term, const char *str);
void terminal_writeline(terminal_buffer_t *term, const char *str);
void terminal_writeline_screen(terminal_buffer_t *term, const char *str);
void terminal_setline(terminal_buffer_t *term, uint8_t line, const char *str);
void terminal_render(void *data);
void terminal_scroll(terminal_buffer_t *term, int32_t pixels);

#endif
