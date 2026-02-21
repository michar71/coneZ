#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "editor.h"
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
    char search[ED_LINE_MAX];       // last search string
    bool search_valid;              // true if search[] is populated
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

    char fpath[128];
    lfs_path(fpath, sizeof(fpath), path);
    FILE *f = fopen(fpath, "r");
    if (f) {
        char buf[ED_LINE_MAX];
        while (fgets(buf, sizeof(buf), f) && ed->num_lines < ED_MAX_LINES) {
            int len = strlen(buf);
            // Strip trailing newline/CR
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
                buf[--len] = '\0';

            if (!ed_ensure_capacity(ed, ed->num_lines + 1)) break;
            psram_write(ed_line_addr(ed, ed->num_lines), (const uint8_t *)buf, ED_LINE_MAX);
            ed->num_lines++;
        }
        fclose(f);
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

    char fpath[128];
    lfs_path(fpath, sizeof(fpath), ed->filepath);
    FILE *f = fopen(fpath, "w");
    if (!f) return false;

    char buf[ED_LINE_MAX];
    for (int i = 0; i < ed->num_lines; i++) {
        psram_read(ed_line_addr(ed, i), (uint8_t *)buf, ED_LINE_MAX);
        buf[ED_LINE_MAX - 1] = '\0';
        fputs(buf, f);
        fputc('\n', f);
    }
    fclose(f);
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

static void editor_draw(EditorState *ed, ConezStream *out)
{
    char buf[ED_COLS + 128];
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
        out->print("\033[7m");
        out->print(buf);
        out->print("\033[0m\033[K\r\n");
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
            out->print("\033[38;5;240m~\033[0m");
        }
        out->print("\033[K\r\n");
    }

    // --- Row 24: help bar (reverse video) ---
    out->print("\033[7m ^W Save  ^X Quit  ^K Cut  ^U Paste  ^F Find  ^G GoTo");
    int help_len = 55;
    for (int i = help_len; i < ED_COLS; i++) out->write(' ');
    out->print("\033[0m\033[K");

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
// Prompt helper — show prompt on status bar, read input
// ---------------------------------------------------------------------------

// Returns length of input, -1 on cancel (ESC/^X).
static int editor_prompt_input(EditorState *ed, ConezStream *out,
                               const char *prompt, char *buf, int maxlen)
{
    int pos = 0;
    buf[0] = '\0';
    int prompt_len = (int)strlen(prompt);

    getLock();
    out->printf("\033[%d;1H\033[7m %s\033[K\033[0m", ED_TOP_ROW, prompt);
    out->printf("\033[%d;%dH\033[?25h", ED_TOP_ROW, prompt_len + 2);
    releaseLock();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (!out->available()) continue;
        int c = out->read();

        if (c == '\r' || c == '\n') {
            return pos;
        } else if (c == 0x1B || c == 0x18) {
            vTaskDelay(pdMS_TO_TICKS(10));
            while (out->available()) out->read();
            return -1;
        } else if (c == 127 || c == '\b') {
            if (pos > 0) {
                buf[--pos] = '\0';
                getLock();
                out->print("\b \b");
                releaseLock();
            }
        } else if (c >= 32 && c < 127 && pos < maxlen - 1) {
            buf[pos++] = c;
            buf[pos] = '\0';
            getLock();
            out->write(c);
            releaseLock();
        }
    }
}

// ---------------------------------------------------------------------------
// Go-to-line prompt
// ---------------------------------------------------------------------------

static void editor_goto_line(EditorState *ed, ConezStream *out)
{
    char buf[16];
    int len = editor_prompt_input(ed, out, "Go to line:", buf, sizeof(buf));
    if (len <= 0) return;

    int target = atoi(buf) - 1;
    if (target < 0) target = 0;
    if (target >= ed->num_lines) target = ed->num_lines - 1;
    ed->cy = target;
    ed->cx = 0;
}

// ---------------------------------------------------------------------------
// Find / Replace
// ---------------------------------------------------------------------------

// Search forward from (cy, cx+1) wrapping around. Returns true if found.
static bool editor_find_next(EditorState *ed)
{
    if (!ed->search_valid) return false;
    int slen = (int)strlen(ed->search);
    if (slen == 0) return false;

    char buf[ED_LINE_MAX];
    int start_line = ed->cy;
    int start_col = ed->cx + 1;

    // Search from current line forward
    for (int i = 0; i < ed->num_lines; i++) {
        int line = (start_line + i) % ed->num_lines;
        ed_read_line(ed, line, buf);
        buf[ED_LINE_MAX - 1] = '\0';

        int col_start = (i == 0) ? start_col : 0;
        int line_len = (int)strlen(buf);
        if (col_start > line_len) continue;

        char *found = strstr(buf + col_start, ed->search);
        if (found) {
            ed->cy = line;
            ed->cx = (int)(found - buf);
            bool wrapped = (line < start_line || (line == start_line && ed->cx < start_col - 1));
            if (wrapped)
                snprintf(ed->status_msg, sizeof(ed->status_msg),
                         "Wrapped — found on line %d", line + 1);
            else
                snprintf(ed->status_msg, sizeof(ed->status_msg),
                         "Found on line %d", line + 1);
            return true;
        }
    }

    snprintf(ed->status_msg, sizeof(ed->status_msg), "Not found");
    return false;
}

static void editor_find_or_replace(EditorState *ed, ConezStream *out)
{
    char input[ED_LINE_MAX];
    int len = editor_prompt_input(ed, out, "Find:", input, sizeof(input));
    if (len < 0) return;   // cancelled

    if (len == 0) {
        // Empty input — repeat last search
        if (!ed->search_valid) return;
        editor_find_next(ed);
        return;
    }

    // Store new search
    strncpy(ed->search, input, ED_LINE_MAX - 1);
    ed->search[ED_LINE_MAX - 1] = '\0';
    ed->search_valid = true;

    // Ask find or replace
    getLock();
    out->printf("\033[%d;1H\033[7m (F)ind or (R)eplace? \033[K\033[0m", ED_TOP_ROW);
    out->printf("\033[%d;22H\033[?25h", ED_TOP_ROW);
    releaseLock();

    int mode = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (!out->available()) continue;
        int c = out->read();
        if (c == 'f' || c == 'F') { mode = 1; break; }
        if (c == 'r' || c == 'R') { mode = 2; break; }
        if (c == 0x1B || c == 0x18) {
            vTaskDelay(pdMS_TO_TICKS(10));
            while (out->available()) out->read();
            return;
        }
    }

    if (mode == 1) {
        editor_find_next(ed);
        return;
    }

    // Replace mode
    char replacement[ED_LINE_MAX];
    int rlen = editor_prompt_input(ed, out, "Replace with:", replacement, sizeof(replacement));
    if (rlen < 0) return;

    int search_len = (int)strlen(ed->search);
    int replace_len = (int)strlen(replacement);
    int count = 0;
    bool replace_all = false;

    if (!editor_find_next(ed)) return;

    for (;;) {
        if (!replace_all) {
            // Redraw to show cursor at match
            editor_ensure_visible(ed);
            getLock();
            editor_draw(ed, out);
            out->printf("\033[%d;1H\033[7m Replace? (y/n/a/q) \033[K\033[0m\033[?25h", ED_TOP_ROW);
            releaseLock();

            int choice = 0;
            for (;;) {
                vTaskDelay(pdMS_TO_TICKS(20));
                if (!out->available()) continue;
                int c = out->read();
                if (c == 'y' || c == 'Y') { choice = 1; break; }
                if (c == 'n' || c == 'N') { choice = 2; break; }
                if (c == 'a' || c == 'A') { choice = 3; break; }
                if (c == 'q' || c == 'Q' || c == 0x1B) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                    while (out->available()) out->read();
                    choice = 4;
                    break;
                }
            }

            if (choice == 4) break;
            if (choice == 3) replace_all = true;
            if (choice == 2) {
                if (!editor_find_next(ed)) break;
                continue;
            }
            // choice == 1 or 3: fall through to replace
        }

        // Perform replacement on current line
        ed_load_work(ed, ed->cy);
        int line_len = (int)strlen(ed->work);
        if (line_len - search_len + replace_len >= ED_LINE_MAX) {
            snprintf(ed->status_msg, sizeof(ed->status_msg), "Line too long, skipped");
            if (!editor_find_next(ed)) break;
            continue;
        }

        memmove(ed->work + ed->cx + replace_len,
                ed->work + ed->cx + search_len,
                line_len - ed->cx - search_len + 1);
        memcpy(ed->work + ed->cx, replacement, replace_len);
        ed->work_dirty = true;
        ed->modified = true;
        count++;

        // Position cursor after replacement for next search
        ed->cx += replace_len - 1;
        if (ed->cx < 0) ed->cx = 0;

        if (!editor_find_next(ed)) break;
    }

    if (count > 0)
        snprintf(ed->status_msg, sizeof(ed->status_msg), "Replaced %d occurrence%s", count, count == 1 ? "" : "s");
}

// ---------------------------------------------------------------------------
// Line editor (non-ANSI fallback)
// ---------------------------------------------------------------------------

static void line_editor_list(EditorState *ed, int from, int to)
{
    char buf[ED_LINE_MAX];
    if (from < 0) from = 0;
    if (to >= ed->num_lines) to = ed->num_lines - 1;
    for (int i = from; i <= to; i++) {
        ed_read_line(ed, i, buf);
        buf[ED_LINE_MAX - 1] = '\0';
        printfnl(SOURCE_NONE, "%3d: %s\n", i + 1, buf);
    }
}

// Read a line of input from the shell stream, blocking.
// Returns length, or -1 if stream closes.
static int line_editor_readline(ConezStream *s, char *buf, int maxlen)
{
    int pos = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        while (s->available()) {
            int c = s->read();
            if (c == '\r' || c == '\n') {
                buf[pos] = '\0';
                getLock();
                s->print("\n");
                releaseLock();
                return pos;
            }
            if ((c == 127 || c == '\b') && pos > 0) {
                pos--;
                getLock();
                s->print("\b \b");
                releaseLock();
            } else if (c >= 32 && c < 127 && pos < maxlen - 1) {
                buf[pos++] = (char)c;
                getLock();
                s->write((uint8_t)c);
                releaseLock();
            }
        }
    }
}

static int line_editor(EditorState *ed)
{
    ConezStream *s = getStream();
    bool modified = ed->modified;
    char cmd[ED_LINE_MAX];

    printfnl(SOURCE_NONE, "Editing %s (%d lines)\n", ed->filepath, ed->num_lines);
    line_editor_list(ed, 0, ed->num_lines - 1);

    for (;;) {
        getLock();
        s->print("edit> ");
        releaseLock();

        int len = line_editor_readline(s, cmd, sizeof(cmd));
        if (len < 0) break;

        // Trim leading whitespace
        char *p = cmd;
        while (*p == ' ' || *p == '\t') p++;

        // Empty line or 'l' — list all
        if (*p == '\0' || (*p == 'l' && p[1] == '\0')) {
            line_editor_list(ed, 0, ed->num_lines - 1);
            continue;
        }

        // 'q' — quit (with unsaved check)
        if (*p == 'q') {
            if (p[1] == '!') break;
            if (modified) {
                printfnl(SOURCE_NONE, "Unsaved changes. Use 'q!' to discard, or 'w' to save first.\n");
                continue;
            }
            break;
        }

        // 'w' — save
        if (*p == 'w' && (p[1] == '\0' || p[1] == ' ')) {
            if (editor_save(ed)) {
                modified = false;
                printfnl(SOURCE_NONE, "Saved %d lines\n", ed->num_lines);
            } else {
                printfnl(SOURCE_NONE, "Save FAILED\n");
            }
            continue;
        }

        // 'p N M' — print range
        if (*p == 'p' && p[1] == ' ') {
            int n1 = 0, n2 = 0;
            if (sscanf(p + 2, "%d %d", &n1, &n2) == 2 && n1 >= 1 && n2 >= n1) {
                line_editor_list(ed, n1 - 1, n2 - 1);
            } else {
                printfnl(SOURCE_NONE, "Usage: p <from> <to>\n");
            }
            continue;
        }

        // 'i N' — insert before line N
        if (*p == 'i' && p[1] == ' ') {
            int n = atoi(p + 2);
            if (n < 1 || n > ed->num_lines + 1) {
                printfnl(SOURCE_NONE, "Line %d out of range (1-%d)\n", n, ed->num_lines + 1);
                continue;
            }
            printfnl(SOURCE_NONE, "Insert before line %d (empty line to stop):\n", n);
            int at = n - 1;
            for (;;) {
                getLock();
                s->print("  > ");
                releaseLock();
                char line[ED_LINE_MAX];
                int ll = line_editor_readline(s, line, sizeof(line));
                if (ll <= 0) break;

                if (ed->num_lines >= ED_MAX_LINES) {
                    printfnl(SOURCE_NONE, "Max lines reached\n");
                    break;
                }
                if (!ed_ensure_capacity(ed, ed->num_lines + 1)) {
                    printfnl(SOURCE_NONE, "Out of memory\n");
                    break;
                }
                ed_invalidate_work(ed);
                ed_shift_down(ed, at);
                ed->num_lines++;
                char buf[ED_LINE_MAX];
                memset(buf, 0, ED_LINE_MAX);
                strncpy(buf, line, ED_LINE_MAX - 1);
                ed_write_line(ed, at, buf);
                modified = true;
                at++;
            }
            continue;
        }

        // 'a' — append at end
        if (*p == 'a' && (p[1] == '\0' || p[1] == ' ')) {
            printfnl(SOURCE_NONE, "Append (empty line to stop):\n");
            for (;;) {
                getLock();
                s->print("  > ");
                releaseLock();
                char line[ED_LINE_MAX];
                int ll = line_editor_readline(s, line, sizeof(line));
                if (ll <= 0) break;

                if (ed->num_lines >= ED_MAX_LINES) {
                    printfnl(SOURCE_NONE, "Max lines reached\n");
                    break;
                }
                if (!ed_ensure_capacity(ed, ed->num_lines + 1)) {
                    printfnl(SOURCE_NONE, "Out of memory\n");
                    break;
                }
                char buf[ED_LINE_MAX];
                memset(buf, 0, ED_LINE_MAX);
                strncpy(buf, line, ED_LINE_MAX - 1);
                ed_write_line(ed, ed->num_lines, buf);
                ed->num_lines++;
                modified = true;
            }
            continue;
        }

        // 'd N' — delete line
        if (*p == 'd' && p[1] == ' ') {
            int n = atoi(p + 2);
            if (n < 1 || n > ed->num_lines) {
                printfnl(SOURCE_NONE, "Line %d out of range (1-%d)\n", n, ed->num_lines);
                continue;
            }
            ed_invalidate_work(ed);
            if (ed->num_lines == 1) {
                char empty[ED_LINE_MAX];
                memset(empty, 0, ED_LINE_MAX);
                ed_write_line(ed, 0, empty);
            } else {
                ed_shift_up(ed, n - 1);
                ed->num_lines--;
            }
            modified = true;
            printfnl(SOURCE_NONE, "Deleted line %d\n", n);
            continue;
        }

        // 'r N' — replace line
        if (*p == 'r' && p[1] == ' ') {
            int n = atoi(p + 2);
            if (n < 1 || n > ed->num_lines) {
                printfnl(SOURCE_NONE, "Line %d out of range (1-%d)\n", n, ed->num_lines);
                continue;
            }
            printfnl(SOURCE_NONE, "Replace line %d:\n", n);
            getLock();
            s->print("  > ");
            releaseLock();
            char line[ED_LINE_MAX];
            line_editor_readline(s, line, sizeof(line));
            char buf[ED_LINE_MAX];
            memset(buf, 0, ED_LINE_MAX);
            strncpy(buf, line, ED_LINE_MAX - 1);
            ed_write_line(ed, n - 1, buf);
            modified = true;
            continue;
        }

        // 'f text' — find next occurrence
        if (*p == 'f' && p[1] == ' ' && p[2] != '\0') {
            char *needle = p + 2;
            strncpy(ed->search, needle, ED_LINE_MAX - 1);
            ed->search[ED_LINE_MAX - 1] = '\0';
            ed->search_valid = true;

            char buf[ED_LINE_MAX];
            // Search from current cy+1 wrapping around
            for (int i = 0; i < ed->num_lines; i++) {
                int line = (ed->cy + 1 + i) % ed->num_lines;
                ed_read_line(ed, line, buf);
                buf[ED_LINE_MAX - 1] = '\0';
                if (strstr(buf, needle)) {
                    ed->cy = line;
                    printfnl(SOURCE_NONE, "%3d: %s\n", line + 1, buf);
                    goto next_cmd;
                }
            }
            printfnl(SOURCE_NONE, "Not found\n");
            next_cmd:
            continue;
        }

        // 's old new' — replace all occurrences
        if (*p == 's' && p[1] == ' ' && p[2] != '\0') {
            // Parse: s old new (space-separated)
            char *old_str = p + 2;
            char *space = strchr(old_str, ' ');
            if (!space || space[1] == '\0') {
                printfnl(SOURCE_NONE, "Usage: s <old> <new>\n");
                continue;
            }
            *space = '\0';
            char *new_str = space + 1;
            int old_len = (int)strlen(old_str);
            int new_len = (int)strlen(new_str);
            int count = 0;

            char buf[ED_LINE_MAX];
            for (int i = 0; i < ed->num_lines; i++) {
                ed_invalidate_work(ed);
                ed_read_line(ed, i, buf);
                buf[ED_LINE_MAX - 1] = '\0';

                char result[ED_LINE_MAX];
                int rpos = 0;
                char *src = buf;
                bool changed = false;
                while (*src) {
                    char *found = strstr(src, old_str);
                    if (found) {
                        int prefix = (int)(found - src);
                        if (rpos + prefix + new_len >= ED_LINE_MAX - 1) break;
                        memcpy(result + rpos, src, prefix);
                        rpos += prefix;
                        memcpy(result + rpos, new_str, new_len);
                        rpos += new_len;
                        src = found + old_len;
                        count++;
                        changed = true;
                    } else {
                        int remain = (int)strlen(src);
                        if (rpos + remain >= ED_LINE_MAX - 1) remain = ED_LINE_MAX - 1 - rpos;
                        memcpy(result + rpos, src, remain);
                        rpos += remain;
                        break;
                    }
                }
                if (changed) {
                    result[rpos] = '\0';
                    memset(result + rpos + 1, 0, ED_LINE_MAX - rpos - 1);
                    ed_write_line(ed, i, result);
                    modified = true;
                }
            }
            printfnl(SOURCE_NONE, "Replaced %d occurrence%s\n", count, count == 1 ? "" : "s");
            continue;
        }

        // Bare number — show that line
        if (*p >= '0' && *p <= '9') {
            int n = atoi(p);
            if (n >= 1 && n <= ed->num_lines) {
                line_editor_list(ed, n - 1, n - 1);
            } else {
                printfnl(SOURCE_NONE, "Line %d out of range (1-%d)\n", n, ed->num_lines);
            }
            continue;
        }

        printfnl(SOURCE_NONE, "Commands: l | N | i N | a | d N | r N | p N M | f text | s old new | w | q\n");
    }

    ed->modified = modified;
    return 0;
}

// ---------------------------------------------------------------------------
// Main editor command
// ---------------------------------------------------------------------------

int cmd_edit(int argc, char **argv)
{
    if (argc < 2) {
        printfnl(SOURCE_COMMANDS, "Usage: edit <filename>\n");
        return 1;
    }

    // Normalize path: prepend '/' if missing (LittleFS requires absolute paths)
    char path[64];
    normalize_path(path, sizeof(path), argv[1]);

    EditorState ed;
    memset(&ed, 0, sizeof(ed));
    ed.work_line = -1;

    if (!editor_load(&ed, path)) {
        printfnl(SOURCE_COMMANDS, "Failed to allocate PSRAM for editor\n");
        return 1;
    }

    // Non-ANSI: use line editor fallback
    if (!getAnsiEnabled()) {
        int result = line_editor(&ed);
        ed_free_blocks(&ed);
        return result;
    }

    setInteractive(true);

    // Drain leftover input
    vTaskDelay(pdMS_TO_TICKS(50));
    while (getStream()->available()) getStream()->read();

    getLock();
    ConezStream *out = getStream();
    out->print("\033[?25l\033[2J");
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
            out->print("\033[?25h");
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

                case 0x06:  // Ctrl-F — Find/Replace
                    editor_find_or_replace(&ed, getStream());
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
    out->print("\033[?25h\033[0m\033[2J\033[H");
    releaseLock();

    return 0;
}
