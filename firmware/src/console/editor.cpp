#include "editor.h"
#include <LittleFS.h>
#include <FS.h>
#include "printManager.h"
#include "shell.h"
#include "psram.h"
#include "main.h"

#define ED_MAX_LINES    512
#define ED_LINE_MAX     256
#define ED_COLS         80
#define ED_ROWS         24
#define ED_TOP_ROW      2               // skip row 1 (PlatformIO terminal quirk)
#define ED_CONTENT_ROWS (ED_ROWS - 3)   // 21 content rows (status + help + blank top)

// PSRAM block management: 64KB blocks, each holds 256 fixed-size lines
#define ED_BLOCK_SIZE       65536
#define ED_LINES_PER_BLOCK  (ED_BLOCK_SIZE / ED_LINE_MAX)   // 256
#define ED_MAX_BLOCKS       ((ED_MAX_LINES + ED_LINES_PER_BLOCK - 1) / ED_LINES_PER_BLOCK)

struct EditorState {
    uint32_t blocks[ED_MAX_BLOCKS]; // PSRAM block addresses
    int num_blocks;                 // allocated blocks
    int num_lines;
    int cx, cy;                     // cursor col/row in file coordinates
    int scroll_y;                   // first visible line index
    int scroll_x;                   // horizontal scroll offset
    bool modified;
    bool quit_pending;
    char filepath[64];
    char clipboard[ED_LINE_MAX];
    bool clipboard_valid;
    char status_msg[ED_COLS + 1];
    char work[ED_LINE_MAX];         // DRAM working buffer for cursor line
    int work_line;                  // line index in work (-1 = none)
    bool work_dirty;                // work needs flushing to PSRAM
};

// ---------------------------------------------------------------------------
// PSRAM line storage
// ---------------------------------------------------------------------------

static uint32_t ed_line_addr(EditorState *ed, int n)
{
    return ed->blocks[n / ED_LINES_PER_BLOCK]
         + (n % ED_LINES_PER_BLOCK) * ED_LINE_MAX;
}

// Allocate PSRAM blocks on demand for at least n lines.
// Returns true on success.
static bool ed_ensure_capacity(EditorState *ed, int n)
{
    int needed = (n + ED_LINES_PER_BLOCK - 1) / ED_LINES_PER_BLOCK;
    if (needed > ED_MAX_BLOCKS) return false;
    while (ed->num_blocks < needed) {
        uint32_t addr = psram_malloc(ED_BLOCK_SIZE);
        if (!addr) return false;
        psram_memset(addr, 0, ED_BLOCK_SIZE);
        ed->blocks[ed->num_blocks++] = addr;
    }
    return true;
}

// Flush the DRAM working buffer back to PSRAM.
static void ed_flush_work(EditorState *ed)
{
    if (ed->work_dirty && ed->work_line >= 0) {
        psram_write(ed_line_addr(ed, ed->work_line),
                    (const uint8_t *)ed->work, ED_LINE_MAX);
        ed->work_dirty = false;
    }
}

// Load line n into the DRAM working buffer (flushes old first).
static void ed_load_work(EditorState *ed, int n)
{
    if (n == ed->work_line) return;
    ed_flush_work(ed);
    psram_read(ed_line_addr(ed, n), (uint8_t *)ed->work, ED_LINE_MAX);
    ed->work_line = n;
    ed->work_dirty = false;
}

// Flush and invalidate the working buffer (call before structural ops).
static void ed_invalidate_work(EditorState *ed)
{
    ed_flush_work(ed);
    ed->work_line = -1;
}

// Read line n into buf.  Returns from DRAM work buffer if it matches.
static void ed_read_line(EditorState *ed, int n, char *buf)
{
    if (n == ed->work_line)
        memcpy(buf, ed->work, ED_LINE_MAX);
    else
        psram_read(ed_line_addr(ed, n), (uint8_t *)buf, ED_LINE_MAX);
}

// Write buf to line n.  Updates work buffer if it's the active line.
static void ed_write_line(EditorState *ed, int n, const char *buf)
{
    if (n == ed->work_line) {
        memcpy(ed->work, buf, ED_LINE_MAX);
        ed->work_dirty = true;
    } else {
        psram_write(ed_line_addr(ed, n), (const uint8_t *)buf, ED_LINE_MAX);
    }
}

// Length of line n (reads from work buffer or PSRAM).
static int ed_line_len(EditorState *ed, int n)
{
    if (n == ed->work_line)
        return (int)strlen(ed->work);
    char buf[ED_LINE_MAX];
    psram_read(ed_line_addr(ed, n), (uint8_t *)buf, ED_LINE_MAX);
    buf[ED_LINE_MAX - 1] = '\0';
    return (int)strlen(buf);
}

// Free all PSRAM blocks.
static void ed_free_blocks(EditorState *ed)
{
    for (int i = 0; i < ed->num_blocks; i++) {
        psram_free(ed->blocks[i]);
        ed->blocks[i] = 0;
    }
    ed->num_blocks = 0;
    ed->num_lines = 0;
    ed->work_line = -1;
    ed->work_dirty = false;
}

// Shift lines [from .. num_lines-1] down by 1 (for insert).
// Caller must ensure capacity for num_lines+1 and flush work first.
static void ed_shift_down(EditorState *ed, int from)
{
    char tmp[ED_LINE_MAX];
    for (int i = ed->num_lines - 1; i >= from; i--) {
        psram_read(ed_line_addr(ed, i), (uint8_t *)tmp, ED_LINE_MAX);
        psram_write(ed_line_addr(ed, i + 1), (const uint8_t *)tmp, ED_LINE_MAX);
    }
}

// Shift lines [from+1 .. num_lines-1] up by 1 (for delete).
// Caller must flush work first.
static void ed_shift_up(EditorState *ed, int from)
{
    char tmp[ED_LINE_MAX];
    for (int i = from; i < ed->num_lines - 1; i++) {
        psram_read(ed_line_addr(ed, i + 1), (uint8_t *)tmp, ED_LINE_MAX);
        psram_write(ed_line_addr(ed, i), (const uint8_t *)tmp, ED_LINE_MAX);
    }
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

static bool editor_load(EditorState *ed, const char *path)
{
    strncpy(ed->filepath, path, sizeof(ed->filepath) - 1);
    ed->filepath[sizeof(ed->filepath) - 1] = '\0';
    ed->num_lines = 0;
    ed->num_blocks = 0;
    ed->work_line = -1;
    ed->work_dirty = false;

    if (!ed_ensure_capacity(ed, 1)) return false;

    File f = LittleFS.open(path, "r");
    if (f) {
        char buf[ED_LINE_MAX];
        while (f.available() && ed->num_lines < ED_MAX_LINES) {
            int len = f.readBytesUntil('\n', buf, sizeof(buf) - 1);
            buf[len] = '\0';
            if (len > 0 && buf[len - 1] == '\r') buf[--len] = '\0';

            if (!ed_ensure_capacity(ed, ed->num_lines + 1)) break;
            psram_write(ed_line_addr(ed, ed->num_lines), (const uint8_t *)buf, ED_LINE_MAX);
            ed->num_lines++;
        }
        f.close();
    }

    // Always have at least one line
    if (ed->num_lines == 0) {
        char empty[ED_LINE_MAX];
        memset(empty, 0, ED_LINE_MAX);
        psram_write(ed_line_addr(ed, 0), (const uint8_t *)empty, ED_LINE_MAX);
        ed->num_lines = 1;
    }

    ed->cx = 0;
    ed->cy = 0;
    ed->scroll_x = 0;
    ed->scroll_y = 0;
    ed->modified = false;
    ed->quit_pending = false;
    ed->clipboard_valid = false;
    ed->status_msg[0] = '\0';
    return true;
}

static bool editor_save(EditorState *ed)
{
    ed_flush_work(ed);

    File f = LittleFS.open(ed->filepath, FILE_WRITE);
    if (!f) return false;

    char buf[ED_LINE_MAX];
    for (int i = 0; i < ed->num_lines; i++) {
        psram_read(ed_line_addr(ed, i), (uint8_t *)buf, ED_LINE_MAX);
        buf[ED_LINE_MAX - 1] = '\0';
        f.print(buf);
        f.print('\n');
    }
    f.close();
    ed->modified = false;
    snprintf(ed->status_msg, sizeof(ed->status_msg), "Saved %d lines", ed->num_lines);
    return true;
}

// ---------------------------------------------------------------------------
// Cursor / scroll
// ---------------------------------------------------------------------------

static void editor_ensure_visible(EditorState *ed)
{
    if (ed->cy < 0) ed->cy = 0;
    if (ed->cy >= ed->num_lines) ed->cy = ed->num_lines - 1;

    // Load cursor line into work buffer so we can check its length
    ed_load_work(ed, ed->cy);
    int line_len = (int)strlen(ed->work);
    if (ed->cx > line_len) ed->cx = line_len;
    if (ed->cx < 0) ed->cx = 0;

    if (ed->cy < ed->scroll_y)
        ed->scroll_y = ed->cy;
    if (ed->cy >= ed->scroll_y + ED_CONTENT_ROWS)
        ed->scroll_y = ed->cy - ED_CONTENT_ROWS + 1;

    int visible_cols = ED_COLS - 1;
    if (ed->cx < ed->scroll_x)
        ed->scroll_x = ed->cx;
    if (ed->cx >= ed->scroll_x + visible_cols)
        ed->scroll_x = ed->cx - visible_cols + 1;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void editor_draw(EditorState *ed, Stream *out)
{
    char buf[ED_COLS + 32];
    char line_buf[ED_LINE_MAX];

    out->printf("\033[%d;1H", ED_TOP_ROW);   // skip row 1

    // --- Status bar (reverse video) ---
    {
        const char *mod = ed->modified ? "*" : "";
        if (ed->status_msg[0]) {
            snprintf(buf, sizeof(buf), " %s%s  %s",
                     ed->filepath, mod, ed->status_msg);
            ed->status_msg[0] = '\0';
        } else {
            snprintf(buf, sizeof(buf), " %s%s    L:%d C:%d",
                     ed->filepath, mod, ed->cy + 1, ed->cx + 1);
        }
        int slen = (int)strlen(buf);
        if (slen < ED_COLS) {
            memset(buf + slen, ' ', ED_COLS - slen);
            buf[ED_COLS] = '\0';
        } else {
            buf[ED_COLS] = '\0';
        }
        out->print(F("\033[7m"));
        out->print(buf);
        out->print(F("\033[0m\033[K\r\n"));
    }

    // --- Rows 2-23: content ---
    for (int row = 0; row < ED_CONTENT_ROWS; row++) {
        int file_line = ed->scroll_y + row;
        if (file_line < ed->num_lines) {
            ed_read_line(ed, file_line, line_buf);
            line_buf[ED_LINE_MAX - 1] = '\0';
            int line_len = (int)strlen(line_buf);
            if (ed->scroll_x < line_len) {
                int avail = line_len - ed->scroll_x;
                int show = (avail > ED_COLS) ? ED_COLS : avail;
                out->write((const uint8_t *)(line_buf + ed->scroll_x), show);
            }
        } else {
            out->print(F("\033[38;5;240m~\033[0m"));
        }
        out->print(F("\033[K\r\n"));
    }

    // --- Row 24: help bar (reverse video) ---
    out->print(F("\033[7m ^W Save  ^X Quit  ^K Cut  ^U Paste  ^G GoTo"));
    int help_len = 46;
    for (int i = help_len; i < ED_COLS; i++) out->write(' ');
    out->print(F("\033[0m\033[K"));

    // --- Position cursor ---
    {
        int screen_row = ed->cy - ed->scroll_y + ED_TOP_ROW + 1;  // +1 for status bar
        int screen_col = ed->cx - ed->scroll_x + 1;
        out->printf("\033[%d;%dH", screen_row, screen_col);
    }
}

// ---------------------------------------------------------------------------
// Editing operations (work on DRAM work buffer for current line)
// ---------------------------------------------------------------------------

static void editor_insert_char(EditorState *ed, char c)
{
    int len = (int)strlen(ed->work);
    if (len >= ED_LINE_MAX - 1) return;

    memmove(ed->work + ed->cx + 1, ed->work + ed->cx, len - ed->cx + 1);
    ed->work[ed->cx] = c;
    ed->cx++;
    ed->work_dirty = true;
    ed->modified = true;
    ed->quit_pending = false;
}

static void editor_backspace(EditorState *ed)
{
    if (ed->cx > 0) {
        int len = (int)strlen(ed->work);
        memmove(ed->work + ed->cx - 1, ed->work + ed->cx, len - ed->cx + 1);
        ed->cx--;
        ed->work_dirty = true;
        ed->modified = true;
    } else if (ed->cy > 0) {
        // Join with previous line
        char prev[ED_LINE_MAX];
        ed_invalidate_work(ed);
        ed_read_line(ed, ed->cy - 1, prev);
        char cur[ED_LINE_MAX];
        ed_read_line(ed, ed->cy, cur);
        int prev_len = (int)strlen(prev);
        int cur_len = (int)strlen(cur);
        if (prev_len + cur_len >= ED_LINE_MAX) return;

        memcpy(prev + prev_len, cur, cur_len + 1);
        ed_write_line(ed, ed->cy - 1, prev);
        ed_shift_up(ed, ed->cy);
        ed->num_lines--;
        ed->cy--;
        ed->cx = prev_len;
        ed->modified = true;
    }
    ed->quit_pending = false;
}

static void editor_delete_char(EditorState *ed)
{
    int len = (int)strlen(ed->work);

    if (ed->cx < len) {
        memmove(ed->work + ed->cx, ed->work + ed->cx + 1, len - ed->cx);
        ed->work_dirty = true;
        ed->modified = true;
    } else if (ed->cy < ed->num_lines - 1) {
        // Join with next line
        char cur[ED_LINE_MAX];
        ed_invalidate_work(ed);
        ed_read_line(ed, ed->cy, cur);
        char next[ED_LINE_MAX];
        ed_read_line(ed, ed->cy + 1, next);
        int cur_len = (int)strlen(cur);
        int next_len = (int)strlen(next);
        if (cur_len + next_len >= ED_LINE_MAX) return;

        memcpy(cur + cur_len, next, next_len + 1);
        ed_write_line(ed, ed->cy, cur);
        ed_shift_up(ed, ed->cy + 1);
        ed->num_lines--;
        ed->modified = true;
    }
    ed->quit_pending = false;
}

static void editor_enter(EditorState *ed)
{
    if (ed->num_lines >= ED_MAX_LINES) return;
    if (!ed_ensure_capacity(ed, ed->num_lines + 1)) return;

    char cur[ED_LINE_MAX];
    ed_invalidate_work(ed);
    ed_read_line(ed, ed->cy, cur);

    // Tail goes to new line below
    char tail[ED_LINE_MAX];
    memset(tail, 0, ED_LINE_MAX);
    strncpy(tail, cur + ed->cx, ED_LINE_MAX - 1);

    // Truncate current line at cursor
    cur[ed->cx] = '\0';
    ed_write_line(ed, ed->cy, cur);

    // Make room and insert tail
    ed_shift_down(ed, ed->cy + 1);
    ed->num_lines++;
    ed_write_line(ed, ed->cy + 1, tail);

    ed->cy++;
    ed->cx = 0;
    ed->modified = true;
    ed->quit_pending = false;
}

static void editor_cut_line(EditorState *ed)
{
    if (ed->num_lines <= 0) return;

    ed_invalidate_work(ed);

    // Copy to clipboard
    ed_read_line(ed, ed->cy, ed->clipboard);
    ed->clipboard[ED_LINE_MAX - 1] = '\0';
    ed->clipboard_valid = true;

    if (ed->num_lines == 1) {
        char empty[ED_LINE_MAX];
        memset(empty, 0, ED_LINE_MAX);
        ed_write_line(ed, 0, empty);
        ed->cx = 0;
    } else {
        ed_shift_up(ed, ed->cy);
        ed->num_lines--;
        if (ed->cy >= ed->num_lines) ed->cy = ed->num_lines - 1;
    }
    ed->cx = 0;
    ed->modified = true;
    ed->quit_pending = false;
    snprintf(ed->status_msg, sizeof(ed->status_msg), "Line cut");
}

static void editor_paste_line(EditorState *ed)
{
    if (!ed->clipboard_valid) return;
    if (ed->num_lines >= ED_MAX_LINES) return;
    if (!ed_ensure_capacity(ed, ed->num_lines + 1)) return;

    ed_invalidate_work(ed);

    ed_shift_down(ed, ed->cy);
    ed->num_lines++;
    ed_write_line(ed, ed->cy, ed->clipboard);

    ed->cx = 0;
    ed->modified = true;
    ed->quit_pending = false;
    snprintf(ed->status_msg, sizeof(ed->status_msg), "Line pasted");
}

// ---------------------------------------------------------------------------
// Go-to-line prompt
// ---------------------------------------------------------------------------

static void editor_goto_line(EditorState *ed, Stream *out)
{
    char buf[16];
    int pos = 0;
    buf[0] = '\0';

    getLock();
    out->printf("\033[%d;1H\033[7m Go to line: \033[K\033[0m", ED_TOP_ROW);
    out->printf("\033[%d;14H\033[?25h", ED_TOP_ROW);
    releaseLock();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (!out->available()) continue;
        int c = out->read();

        if (c == '\r' || c == '\n') {
            break;
        } else if (c == 0x1B || c == 0x18) {
            // Drain remaining escape sequence bytes (e.g., ESC[A from arrow keys)
            vTaskDelay(pdMS_TO_TICKS(10));
            while (out->available()) out->read();
            return;
        } else if (c == 127 || c == '\b') {
            if (pos > 0) {
                buf[--pos] = '\0';
                getLock();
                out->print(F("\b \b"));
                releaseLock();
            }
        } else if (c >= '0' && c <= '9' && pos < 6) {
            buf[pos++] = c;
            buf[pos] = '\0';
            getLock();
            out->write(c);
            releaseLock();
        }
    }

    if (pos > 0) {
        int target = atoi(buf) - 1;
        if (target < 0) target = 0;
        if (target >= ed->num_lines) target = ed->num_lines - 1;
        ed->cy = target;
        ed->cx = 0;
    }
}

// ---------------------------------------------------------------------------
// Main editor command
// ---------------------------------------------------------------------------

int cmd_edit(int argc, char **argv)
{
    if (argc < 2) {
        printfnl(SOURCE_COMMANDS, F("Usage: edit <filename>\n"));
        return 1;
    }

    // Normalize path: prepend '/' if missing (LittleFS requires absolute paths)
    char path[64];
    normalize_path(path, sizeof(path), argv[1]);

    EditorState ed;
    memset(&ed, 0, sizeof(ed));
    ed.work_line = -1;

    if (!editor_load(&ed, path)) {
        printfnl(SOURCE_COMMANDS, F("Failed to allocate PSRAM for editor\n"));
        return 1;
    }

    setInteractive(true);

    // Drain leftover input
    vTaskDelay(pdMS_TO_TICKS(50));
    while (getStream()->available()) getStream()->read();

    getLock();
    Stream *out = getStream();
    out->print(F("\033[?25l\033[2J"));
    releaseLock();

    bool running = true;
    bool dirty = true;
    int escState = 0;

    while (running) {
        if (dirty) {
            editor_ensure_visible(&ed);

            getLock();
            out = getStream();
            editor_draw(&ed, out);
            out->print(F("\033[?25h"));
            releaseLock();
            dirty = false;
        }

        vTaskDelay(pdMS_TO_TICKS(20));

        while (getStream()->available()) {
            dirty = true;
            int c = getStream()->read();
            if (c <= 0) continue;

            // Escape sequence state machine
            if (escState == 1) {
                if (c == '[') escState = 2;
                else escState = 0;
                continue;
            }
            if (escState == 2) {
                escState = 0;
                switch (c) {
                    case 'A':   // Up
                        ed.cy--;
                        ed.quit_pending = false;
                        break;
                    case 'B':   // Down
                        ed.cy++;
                        ed.quit_pending = false;
                        break;
                    case 'C':   // Right
                        if (ed.cx < ed_line_len(&ed, ed.cy))
                            ed.cx++;
                        else if (ed.cy < ed.num_lines - 1) {
                            ed.cy++;
                            ed.cx = 0;
                        }
                        ed.quit_pending = false;
                        break;
                    case 'D':   // Left
                        if (ed.cx > 0)
                            ed.cx--;
                        else if (ed.cy > 0) {
                            ed.cy--;
                            ed.cx = ed_line_len(&ed, ed.cy);
                        }
                        ed.quit_pending = false;
                        break;
                    case 'H':   // Home
                        ed.cx = 0;
                        break;
                    case 'F':   // End
                        ed.cx = ed_line_len(&ed, ed.cy);
                        break;
                    case '3':   // Delete (ESC[3~)
                        escState = 3;
                        break;
                    case '5':   // PgUp (ESC[5~)
                        escState = 5;
                        break;
                    case '6':   // PgDn (ESC[6~)
                        escState = 6;
                        break;
                    default:
                        if (c >= '0' && c <= '9') escState = 4;
                        break;
                }
                continue;
            }
            if (escState == 3) {
                escState = 0;
                if (c == '~') editor_delete_char(&ed);
                continue;
            }
            if (escState == 4) {
                escState = 0;
                continue;
            }
            if (escState == 5) {
                escState = 0;
                if (c == '~') {
                    ed.cy -= ED_CONTENT_ROWS;
                    ed.quit_pending = false;
                }
                continue;
            }
            if (escState == 6) {
                escState = 0;
                if (c == '~') {
                    ed.cy += ED_CONTENT_ROWS;
                    ed.quit_pending = false;
                }
                continue;
            }

            switch (c) {
                case 0x1B:
                    escState = 1;
                    break;

                case 0x01:  // Ctrl-A — Home
                    ed.cx = 0;
                    break;

                case 0x05:  // Ctrl-E — End
                    ed.cx = ed_line_len(&ed, ed.cy);
                    break;

                case 0x17:  // Ctrl-W — Save
                    if (!editor_save(&ed))
                        snprintf(ed.status_msg, sizeof(ed.status_msg), "Save FAILED");
                    break;

                case 0x03:  // Ctrl-C — Quit
                case 0x18:  // Ctrl-X — Quit
                    if (ed.modified && !ed.quit_pending) {
                        ed.quit_pending = true;
                        snprintf(ed.status_msg, sizeof(ed.status_msg),
                                 "Unsaved changes! ^X/^C again to discard");
                    } else {
                        running = false;
                    }
                    break;

                case 0x0B:  // Ctrl-K — Cut line
                    editor_cut_line(&ed);
                    break;

                case 0x15:  // Ctrl-U — Paste line
                    editor_paste_line(&ed);
                    break;

                case 0x07:  // Ctrl-G — Go to line
                    editor_goto_line(&ed, getStream());
                    break;

                case '\r':
                    editor_enter(&ed);
                    break;

                case '\n':
                    break;

                case 127:
                case '\b':
                    editor_backspace(&ed);
                    break;

                case '\t':
                    for (int i = 0; i < 4; i++)
                        editor_insert_char(&ed, ' ');
                    break;

                default:
                    if (c >= 32 && c < 127)
                        editor_insert_char(&ed, (char)c);
                    break;
            }
        }
    }

    ed_free_blocks(&ed);
    setInteractive(false);

    getLock();
    out = getStream();
    out->print(F("\033[?25h\033[0m\033[2J\033[H"));
    releaseLock();

    return 0;
}
