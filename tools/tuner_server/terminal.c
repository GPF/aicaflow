#include <enDjinn/enj_font.h>

#ifndef ENJ_INJECT_QFONT
#define ENJ_INJECT_QFONT
#endif
#include <enDjinn/enj_qfont.h>

#include "terminal.h"

void terminal_clear(terminal_buffer_t *term) {
    for (int y = 0; y < TERMINAL_LINES_CAPACITY; ++y) term->line_lengths[y] = 0;
    term->cur_line = term->cur_col = 0;
    term->auto_scroll = term->user_scroll = 0;
}

static void terminal_newline(terminal_buffer_t *term) {
    term->line_lengths[term->cur_line] = term->cur_col;
    term->cur_line = (term->cur_line + 1u) % TERMINAL_LINES_CAPACITY;
    term->cur_col = 0;
    ++term->auto_scroll;
}

void terminal_write(terminal_buffer_t *term, const char *str) {
    while (*str) {
        if (*str == '\n' || term->cur_col >= TERMINAL_WIDTH) {
            terminal_newline(term);
            if (*str != '\n') continue;
        } else {
            term->buffer[term->cur_line][term->cur_col++] = *str;
        }
        ++str;
    }
    term->line_lengths[term->cur_line] = term->cur_col;
}

void terminal_writeline(terminal_buffer_t *term, const char *str) {
    printf("%s\n", str);
    terminal_writeline_screen(term, str);
}

void terminal_writeline_screen(terminal_buffer_t *term, const char *str) {
    terminal_write(term, str);
    terminal_newline(term);
}

void terminal_setline(terminal_buffer_t *term, uint8_t line, const char *str) {
    uint8_t length = 0;
    while (str[length] && length < TERMINAL_WIDTH) term->buffer[line][length] = str[length], ++length;
    term->line_lengths[line] = length;
}

void terminal_render(void *data) {
    terminal_buffer_t *term = data;
    enj_font_header_t *font_hdr = enj_qfont_get_header();
    int scroll = (term->auto_scroll > TERMINAL_LINES_PR_SCREEN ?
                  term->auto_scroll - TERMINAL_LINES_PR_SCREEN : 0) * font_hdr->line_height +
                 term->user_scroll;
    int first_line = scroll / font_hdr->line_height;
    int last_line = first_line + TERMINAL_LINES_PR_SCREEN + 1;
    int render_offset_y = -(scroll % font_hdr->line_height);
    for (int y = first_line; y < last_line; ++y) {
        uint32_t line = (uint32_t)y % TERMINAL_LINES_CAPACITY;
        if (term->line_lengths[line]) {
            term->buffer[line][term->line_lengths[line]] = '\0';
            enj_qfont_write(term->buffer[line], TEXT_TMARGIN_LEFT, render_offset_y,
                            PVR_LIST_PT_POLY);
        }
        render_offset_y += font_hdr->line_height;
    }
}

void terminal_scroll(terminal_buffer_t *term, int32_t pixels) {
    term->user_scroll += pixels;
    if (term->user_scroll > 0) term->user_scroll = 0;
    int32_t limit = -(term->auto_scroll > TERMINAL_LINES_PR_SCREEN ?
                      term->auto_scroll - TERMINAL_LINES_PR_SCREEN : 0) *
                    enj_qfont_get_header()->line_height;
    if (term->user_scroll < limit) term->user_scroll = limit;
}
