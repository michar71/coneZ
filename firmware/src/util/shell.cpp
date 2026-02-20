#include <Arduino.h>
#include <LittleFS.h>
#include "shell.h"
#include "printManager.h"
#include "psram.h"

// Prompt and color strings — selected at runtime via getAnsiEnabled()
#define SH_PROMPT_ANSI      "\033[1;32m> \033[33m"      // bold green "> ", then yellow
#define SH_PROMPT_CR_ANSI   "\r\033[1;32m> \033[33m"     // with leading CR
#define SH_RESET_ANSI       "\033[0m"                    // reset to default
#define SH_PROMPT_PLAIN     "> "
#define SH_PROMPT_CR_PLAIN  "\r> "
#define SH_RESET_PLAIN      ""

static inline const char *sh_prompt(void) {
    return getAnsiEnabled() ? SH_PROMPT_ANSI : SH_PROMPT_PLAIN;
}

static inline const char *sh_prompt_cr(void) {
    return getAnsiEnabled() ? SH_PROMPT_CR_ANSI : SH_PROMPT_CR_PLAIN;
}

static inline const char *sh_reset(void) {
    return getAnsiEnabled() ? SH_RESET_ANSI : SH_RESET_PLAIN;
}

////////////////////////////////////////////////////////////////////////////////
// Serial command shell — based on ConezShell by Phil Jansen,
// heavily modified for ConeZ (cursor editing, history, suspend/resume).

// The static instance of the singleton
ConezShell ConezShell::theShell;

// A reference to the singleton shell in the global namespace.
ConezShell& shell = ConezShell::theShell;

//
ConezShell::Command * ConezShell::firstCommand = NULL;

////////////////////////////////////////////////////////////////////////////////
/*!
 *  @brief associates a named command with the function to call.
 */
class ConezShell::Command {
    public:
        Command(const __FlashStringHelper * n, CommandFunction f):
            nameAndDocs(n), myFunc(f) {};

        int execute(int argc, char **argv)
        {
            return myFunc(argc, argv);
        };

        // Comparison used for sort commands
        int compare(const Command * other) const
        {
            const String otherNameString(other->nameAndDocs);
            return compareName(otherNameString.c_str());
        };

        int compareName(const char * aName) const
        {
            String work(nameAndDocs);
            int delim = work.indexOf(' ');
            if (delim >= 0) {
                work.remove(delim);
            }
            return strncasecmp(work.c_str(), aName, SHELL_BUFSIZE);
        };

        void renderDocumentation(Stream& str) const
        {
            getLock();
            str.print(F("  "));
            str.print(nameAndDocs);
            str.println();
            releaseLock();
        }

        // Extract just the command name (before space) into buf.
        // Returns length of name (truncated to bufsz-1).
        int getName(char *buf, int bufsz) const
        {
            const char *p = (const char *)nameAndDocs;
            int i = 0;
            while (i < bufsz - 1) {
                char ch = pgm_read_byte(p + i);
                if (ch == '\0' || ch == ' ') break;
                buf[i++] = ch;
            }
            buf[i] = '\0';
            return i;
        };

        Command * next;

    private:

        const __FlashStringHelper * const nameAndDocs;
        const CommandFunction myFunc;
};

////////////////////////////////////////////////////////////////////////////////
ConezShell::ConezShell()
    : shellConnection(NULL),
      m_lastErrNo(EXIT_SUCCESS),
      tokenizer(strtok_r)
{
    resetBuffer();
    history[0] = '\0';
    hist_addr = 0;
    hist_count = 0;
    hist_write = 0;
    hist_nav = -1;
    inputActive = false;
    lastWasTab = false;

    addCommand(F("history"), ConezShell::printHistory);
};

//////////////////////////////////////////////////////////////////////////////
void ConezShell::addCommand(
    const __FlashStringHelper * name, CommandFunction f)
{
    auto * newCmd = new Command(name, f);

    // insert in list alphabetically
    Command* temp2 = firstCommand;
    Command** temp3 = &firstCommand;
    while (temp2 != NULL && (newCmd->compare(temp2) > 0) )
    {
        temp3 = &temp2->next;
        temp2 = temp2->next;
    }
    *temp3 = newCmd;
    newCmd->next = temp2;
}

//////////////////////////////////////////////////////////////////////////////
bool ConezShell::executeIfInput(void)
{
    bool bufferReady = prepInput();
    bool didSomething = false;

    if (bufferReady)
    {
        didSomething = true;
        // Save command before execute clobbers linebuffer via tokenizer
        char pending[SHELL_BUFSIZE];
        pending[0] = '\0';
        if (linebuffer[0] != '\0')
            strncpy(pending, linebuffer, SHELL_BUFSIZE);
        getLock();
        inputActive = false;
        releaseLock();
        execute();
        // Update history after execute so 'history' command sees previous entry
        if (pending[0] != '\0')
            historyAdd(pending);
        if (shellConnection)
        {
            getLock();
            shellConnection->print(sh_prompt());
            inputActive = true;
            releaseLock();
        } else {
            getLock();
            inputActive = true;
            releaseLock();
        }
    }
    return didSomething;
}

//////////////////////////////////////////////////////////////////////////////
void ConezShell::attach(Stream & requester)
{
    shellConnection = &requester;
}

//////////////////////////////////////////////////////////////////////////////
void ConezShell::showPrompt(void)
{
    getLock();
    if (shellConnection) {
        shellConnection->print(sh_prompt());
    }
    inputActive = true;
    releaseLock();
}

//////////////////////////////////////////////////////////////////////////////
// Process pending input characters.  The print_mutex is held around the
// read, buffer modifications and echo writes so that (a) the underlying
// Serial/Telnet objects are never accessed concurrently from two tasks,
// and (b) printfnl's suspendLine / resumeLine always see consistent
// linebuffer/inptr/cursor state.
bool ConezShell::prepInput(void)
{
    bool bufferReady = false; // assume not ready
    bool moreData = true;

    do {
        // Hold the lock for the entire read + process + echo cycle.
        // read() returns immediately (-1 if nothing), so this only
        // blocks other writers for microseconds per iteration.
        getLock();

        int c = -1;
        if (shellConnection)
            c = shellConnection->read();

        if (c == -1) { releaseLock(); moreData = false; continue; }
        if (c == 0) { releaseLock(); continue; } // throw away NUL

        // Escape sequence state machine
        if (escState == 1) {
            if (c == '[') { escState = 2; }
            else          { escState = 0; } // unknown escape, discard
            releaseLock();
            continue;
        }
        if (escState == 2) {
            escState = 0;
            int prevLen = inptr;
            switch (c) {
                case 'A': // Up arrow — recall older history entry
                    {
                        int next = hist_nav + 1;
                        char hbuf[SHELL_BUFSIZE];
                        if (historyGet(next, hbuf)) {
                            hist_nav = next;
                            strncpy(linebuffer, hbuf, SHELL_BUFSIZE - 1);
                            linebuffer[SHELL_BUFSIZE - 1] = '\0';
                            inptr = strlen(linebuffer);
                            cursor = inptr;
                            redrawLine(prevLen);
                        }
                    }
                    break;
                case 'B': // Down arrow — recall newer history entry or clear
                    {
                        if (hist_nav > 0) {
                            hist_nav--;
                            char hbuf[SHELL_BUFSIZE];
                            if (historyGet(hist_nav, hbuf)) {
                                strncpy(linebuffer, hbuf, SHELL_BUFSIZE - 1);
                                linebuffer[SHELL_BUFSIZE - 1] = '\0';
                                inptr = strlen(linebuffer);
                                cursor = inptr;
                                redrawLine(prevLen);
                            }
                        } else {
                            hist_nav = -1;
                            inptr = 0; cursor = 0;
                            linebuffer[0] = '\0';
                            redrawLine(prevLen);
                        }
                    }
                    break;
                case 'C': // Right arrow
                    if (cursor < inptr && shellConnection) {
                        cursor++;
                        if (getAnsiEnabled())
                            shellConnection->print(F("\033[C"));
                        else {
                            shellConnection->print(sh_prompt_cr());
                            shellConnection->write((const uint8_t*)linebuffer, cursor);
                        }
                    }
                    break;
                case 'D': // Left arrow
                    if (cursor > 0 && shellConnection) {
                        cursor--;
                        if (getAnsiEnabled())
                            shellConnection->print(F("\033[D"));
                        else {
                            shellConnection->print(sh_prompt_cr());
                            shellConnection->write((const uint8_t*)linebuffer, cursor);
                        }
                    }
                    break;
                case 'H': // Home
                    if (shellConnection)
                        shellConnection->print(sh_prompt_cr());
                    cursor = 0;
                    break;
                case 'F': // End
                    if (shellConnection) {
                        if (getAnsiEnabled())
                            for (int i = cursor; i < inptr; i++)
                                shellConnection->print(F("\033[C"));
                        else {
                            shellConnection->print(sh_prompt_cr());
                            shellConnection->write((const uint8_t*)linebuffer, inptr);
                        }
                    }
                    cursor = inptr;
                    break;
                case '3': // Delete key (ESC[3~)
                    escState = 3;
                    break;
                case '2': // Insert (ESC[2~) — ignore
                case '5': // PgUp   (ESC[5~) — ignore
                case '6': // PgDn   (ESC[6~) — ignore
                    escState = 4;
                    break;
                default:  // unknown CSI sequence, discard
                    break;
            }
            releaseLock();
            continue;
        }
        if (escState == 3) {
            escState = 0;
            if (c == '~') {
                // Delete key: remove char at cursor
                if (cursor < inptr) {
                    int prevLen = inptr;
                    memmove(&linebuffer[cursor], &linebuffer[cursor + 1], inptr - cursor - 1);
                    inptr--;
                    linebuffer[inptr] = '\0';
                    redrawLine(prevLen);
                }
            }
            releaseLock();
            continue;
        }
        if (escState == 4) {
            escState = 0;  // consume trailing ~ from Insert/PgUp/PgDn
            releaseLock();
            continue;
        }

        // Normal character processing
        if (c != 0x09) lastWasTab = false;  // reset on any non-tab key
        switch (c)
        {
            case 0x1B: // ESC
                escState = 1;
                break;

            case 127: // DEL key
            case '\b':  // CTRL(H) backspace
                if (cursor > 0) {
                    int prevLen = inptr;
                    if (cursor == inptr) {
                        // simple backspace at end of line
                        if (shellConnection)
                            shellConnection->print(F("\b \b"));
                        linebuffer[--inptr] = '\0';
                        cursor--;
                    } else {
                        // backspace mid-line
                        memmove(&linebuffer[cursor - 1], &linebuffer[cursor], inptr - cursor);
                        cursor--;
                        inptr--;
                        linebuffer[inptr] = '\0';
                        redrawLine(prevLen);
                    }
                }
                break;

            case 0x01: // CTRL-A — Home
                if (shellConnection)
                    shellConnection->print(sh_prompt_cr());
                cursor = 0;
                break;

            case 0x05: // CTRL-E — End
                if (shellConnection) {
                    if (getAnsiEnabled())
                        for (int i = cursor; i < inptr; i++)
                            shellConnection->print(F("\033[C"));
                    else {
                        shellConnection->print(sh_prompt_cr());
                        shellConnection->write((const uint8_t*)linebuffer, inptr);
                    }
                }
                cursor = inptr;
                break;

            case 0x12: //CTRL('R')
                if (shellConnection)
                {
                    shellConnection->print(F("\r\n"));
                    shellConnection->print(sh_prompt());
                    shellConnection->print(linebuffer);
                    cursor = inptr;
                }
                break;

            case 0x15: //CTRL('U')
                {
                    int prevLen = inptr;
                    resetBuffer();
                    redrawLine(prevLen);
                }
                break;

            case '\r':
                inputActive = false;
                hist_nav = -1;
                if (shellConnection) {
                    shellConnection->print(sh_reset());
                    shellConnection->print(F("\n"));
                }
                bufferReady = true;
                break;

            case '\n':
                break;

            case 0x09: // TAB — auto-complete
                releaseLock();   // tabComplete does its own locking
                tabComplete();
                getLock();       // re-acquire for loop epilogue
                break;

            default:
                if (inptr >= SHELL_BUFSIZE - 1) {
                    bufferReady = true;
                    break;
                }
                if (cursor == inptr) {
                    // append at end (fast path)
                    linebuffer[inptr++] = c;
                    linebuffer[inptr] = '\0';
                    cursor++;
                    if (echoEnabled && shellConnection)
                        shellConnection->write(c);
                } else {
                    // insert mid-line
                    int prevLen = inptr;
                    memmove(&linebuffer[cursor + 1], &linebuffer[cursor], inptr - cursor);
                    linebuffer[cursor] = c;
                    inptr++;
                    cursor++;
                    linebuffer[inptr] = '\0';
                    redrawLine(prevLen);
                }
                if (inptr >= SHELL_BUFSIZE - 1) {
                    bufferReady = true;
                }
                break;
        }

        releaseLock();

    } while (moreData && !bufferReady);

    return bufferReady;
}

//////////////////////////////////////////////////////////////////////////////
void ConezShell::setEcho(bool echo)
{
    echoEnabled = echo;
}

//////////////////////////////////////////////////////////////////////////////
int ConezShell::execute(const char commandString[])
{
    // overwrites anything in linebuffer; hope you don't need it!
    strncpy(linebuffer, commandString, SHELL_BUFSIZE);
    linebuffer[SHELL_BUFSIZE - 1] = '\0';
    return execute();
}

//////////////////////////////////////////////////////////////////////////////
int ConezShell::execute(void)
{
    char * argv[MAXARGS] = {0};
    linebuffer[SHELL_BUFSIZE - 1] = '\0'; // play it safe
    int argc = 0;

    char * rest = NULL;
    const char * whitespace = " \t\r\n";
    char * commandName = tokenizer(linebuffer, whitespace, &rest);

    if (!commandName)
    {
        // empty line; just reset and reprompt
        resetBuffer();
        return EXIT_SUCCESS;
    }
    argv[argc++] = commandName;

    for ( ; argc < MAXARGS; )
    {
        char * anArg = tokenizer(0, whitespace, &rest);
        if (anArg) {
            argv[argc++] = anArg;
        } else {
            // no more arguments
            return execute(argc, argv);
        }
    }

    return report(F("Too many arguments to parse"), -1);
}

//////////////////////////////////////////////////////////////////////////////
int ConezShell::execute(int argc, char **argv)
{
    m_lastErrNo = 0;
    for ( Command * aCmd = firstCommand; aCmd != NULL; aCmd = aCmd->next)
    {
        if (aCmd->compareName(argv[0]) == 0)
        {
            m_lastErrNo = aCmd->execute(argc, argv);
            resetBuffer();
            return m_lastErrNo;
        }
    }
    if (shellConnection )
    {
        getLock();
        shellConnection->print(F("\""));
        shellConnection->print(argv[0]);
        shellConnection->print(F("\": "));
        releaseLock();
    }

    return report(F("command not found"), -1);
}

//////////////////////////////////////////////////////////////////////////////
int ConezShell::lastErrNo(void)
{
    return m_lastErrNo;
}

//////////////////////////////////////////////////////////////////////////////
int ConezShell::report(const __FlashStringHelper * constMsg, int errorCode)
{
    if (errorCode != EXIT_SUCCESS)
    {
        String message(constMsg);
        if (shellConnection)
        {
            getLock();
            shellConnection->print(errorCode);
            if (message[0] != '\0') {
                shellConnection->print(F(": "));
                shellConnection->println(message);
            } else {
                shellConnection->println();
            }
            releaseLock();
        }
    }
    resetBuffer();
    m_lastErrNo = errorCode;
    return errorCode;
}
//////////////////////////////////////////////////////////////////////////////
void ConezShell::resetBuffer(void)
{
    memset(linebuffer, 0, sizeof(linebuffer));
    inptr = 0;
    cursor = 0;
    escState = 0;
}

//////////////////////////////////////////////////////////////////////////////
// Called by printManager under mutex — erase the visible prompt+input line
void ConezShell::suspendLine(Stream *out)
{
    if (!inputActive || !out) return;
    if (getAnsiEnabled()) {
        out->print(sh_reset());
        out->print(F("\r\033[K"));
    } else {
        out->print(F("\r"));
        for (int i = 0; i < inptr + 2; i++) out->write(' ');
        out->print(F("\r"));
    }
}

// Called by printManager under mutex — redraw the prompt+input line
void ConezShell::resumeLine(Stream *out)
{
    if (!inputActive || !out) return;
    out->print(sh_prompt());
    out->write((const uint8_t*)linebuffer, inptr);
    // reposition cursor if not at end
    for (int i = cursor; i < inptr; i++) out->write('\b');
}

//////////////////////////////////////////////////////////////////////////////
// Tab completion for commands (first word) and filenames (after space).
// Called WITHOUT print_mutex held — acquires it internally as needed.
void ConezShell::tabComplete(void)
{
    if (!shellConnection) return;

    // Determine context: is there a space before cursor?
    int wordStart = 0;
    bool completingFile = false;
    for (int i = 0; i < cursor; i++) {
        if (linebuffer[i] == ' ') {
            completingFile = true;
            wordStart = i + 1;
        }
    }

    // Extract the prefix being completed
    int prefixLen = cursor - wordStart;
    char prefix[SHELL_BUFSIZE];
    memcpy(prefix, &linebuffer[wordStart], prefixLen);
    prefix[prefixLen] = '\0';

    // Match collection — fixed-size, no heap alloc
    // LittleFS filenames max 31 chars; command names are short too
    static const int MAX_MATCHES = 16;
    static const int MAX_NAME = 32;
    struct Match {
        char name[MAX_NAME];
        int len;
        bool isDir;
    };
    Match matches[MAX_MATCHES];
    int nMatches = 0;

    if (!completingFile) {
        // Command completion — walk the linked list
        getLock();
        for (Command *cmd = firstCommand; cmd && nMatches < MAX_MATCHES; cmd = cmd->next) {
            char name[MAX_NAME];
            int nlen = cmd->getName(name, sizeof(name));
            if (nlen >= prefixLen && strncasecmp(name, prefix, prefixLen) == 0) {
                memcpy(matches[nMatches].name, name, nlen + 1);
                matches[nMatches].len = nlen;
                matches[nMatches].isDir = false;
                nMatches++;
            }
        }
        releaseLock();
    } else {
        // Filename completion — split prefix into dir + partial name
        char dirPath[SHELL_BUFSIZE];
        const char *partial = prefix;
        int partialLen = prefixLen;

        // Find last '/' to split dir from partial
        const char *lastSlash = NULL;
        for (int i = 0; i < prefixLen; i++) {
            if (prefix[i] == '/') lastSlash = &prefix[i];
        }

        if (lastSlash) {
            int dirLen = lastSlash - prefix + 1;
            memcpy(dirPath, prefix, dirLen);
            dirPath[dirLen] = '\0';
            partial = lastSlash + 1;
            partialLen = prefixLen - dirLen;
        } else {
            strcpy(dirPath, "/");
            // partial stays as full prefix
        }

        getLock();
        File root = LittleFS.open(dirPath);
        if (root && root.isDirectory()) {
            File f = root.openNextFile();
            while (f && nMatches < MAX_MATCHES) {
                const char *fname = f.name();
                int flen = strlen(fname);
                bool isDir = f.isDirectory();
                if (flen >= partialLen && flen < MAX_NAME &&
                    strncasecmp(fname, partial, partialLen) == 0) {
                    memcpy(matches[nMatches].name, fname, flen + 1);
                    matches[nMatches].len = flen;
                    matches[nMatches].isDir = isDir;
                    nMatches++;
                }
                f = root.openNextFile();
            }
            root.close();
        }
        releaseLock();
    }

    if (nMatches == 0) {
        lastWasTab = true;
        return;
    }

    // Compute longest common prefix among matches
    int commonLen = matches[0].len;
    for (int i = 1; i < nMatches; i++) {
        int j = 0;
        while (j < commonLen && j < matches[i].len &&
               tolower((unsigned char)matches[0].name[j]) ==
               tolower((unsigned char)matches[i].name[j]))
            j++;
        commonLen = j;
    }

    // How many new characters can we insert?
    // For files, the prefix being completed is just the partial filename part
    int typedPartLen;
    if (completingFile) {
        // Partial is the portion after the last slash
        const char *lastSlash = NULL;
        for (int i = 0; i < prefixLen; i++) {
            if (prefix[i] == '/') lastSlash = &prefix[i];
        }
        typedPartLen = lastSlash ? (prefixLen - (lastSlash - prefix + 1)) : prefixLen;
    } else {
        typedPartLen = prefixLen;
    }

    int extension = commonLen - typedPartLen;

    if (extension > 0) {
        // Insert the new characters into linebuffer at cursor
        // Use the case from the match, not from what was typed
        const char *src = &matches[0].name[typedPartLen];
        int room = SHELL_BUFSIZE - 1 - inptr;
        if (extension > room) extension = room;

        getLock();
        int prevLen = inptr;
        // Make room if cursor is mid-line
        if (cursor < inptr)
            memmove(&linebuffer[cursor + extension], &linebuffer[cursor], inptr - cursor);
        memcpy(&linebuffer[cursor], src, extension);
        inptr += extension;
        cursor += extension;
        linebuffer[inptr] = '\0';

        // If single match, append suffix
        if (nMatches == 1) {
            char suffix = completingFile ? (matches[0].isDir ? '/' : ' ') : ' ';
            if (inptr < SHELL_BUFSIZE - 1) {
                if (cursor < inptr)
                    memmove(&linebuffer[cursor + 1], &linebuffer[cursor], inptr - cursor);
                linebuffer[cursor] = suffix;
                inptr++;
                cursor++;
                linebuffer[inptr] = '\0';
            }
        }

        redrawLine(prevLen);
        releaseLock();
        lastWasTab = false;
    } else if (nMatches == 1) {
        // Exact match, just append suffix
        getLock();
        int prevLen = inptr;
        char suffix = completingFile ? (matches[0].isDir ? '/' : ' ') : ' ';
        if (inptr < SHELL_BUFSIZE - 1) {
            if (cursor < inptr)
                memmove(&linebuffer[cursor + 1], &linebuffer[cursor], inptr - cursor);
            linebuffer[cursor] = suffix;
            inptr++;
            cursor++;
            linebuffer[inptr] = '\0';
            redrawLine(prevLen);
        }
        releaseLock();
        lastWasTab = false;
    } else if (lastWasTab) {
        // Second tab — list all matches
        getLock();
        shellConnection->print(F("\r\n"));
        for (int i = 0; i < nMatches; i++) {
            shellConnection->print(matches[i].name);
            if (matches[i].isDir) shellConnection->write('/');
            shellConnection->print(F("  "));
        }
        shellConnection->print(F("\r\n"));
        // Re-display prompt + current line
        shellConnection->print(sh_prompt());
        shellConnection->write((const uint8_t*)linebuffer, inptr);
        for (int i = cursor; i < inptr; i++) shellConnection->write('\b');
        releaseLock();
        lastWasTab = false;
    } else {
        lastWasTab = true;
    }
}

//////////////////////////////////////////////////////////////////////////////
// Caller must hold print_mutex.
void ConezShell::redrawLine(int prevLen)
{
    if (!shellConnection) return;
    shellConnection->print(sh_prompt_cr());
    shellConnection->write((const uint8_t*)linebuffer, inptr);
    // clear leftover characters from a longer previous line
    for (int i = inptr; i < prevLen; i++) shellConnection->write(' ');
    // reposition cursor
    int backup = (prevLen > inptr ? prevLen : inptr) - cursor;
    for (int i = 0; i < backup; i++) shellConnection->write('\b');
}

//////////////////////////////////////////////////////////////////////////////
// PSRAM-backed history ring buffer

void ConezShell::historyInit(void)
{
    if (hist_addr) return;  // already allocated
    uint32_t size = HIST_MAX * SHELL_BUFSIZE;
    hist_addr = psram_malloc(size);
    if (hist_addr) {
        psram_memset(hist_addr, 0, size);
        // Migrate DRAM fallback entry into ring if it exists
        if (history[0] != '\0') {
            psram_write(hist_addr, (const uint8_t *)history, SHELL_BUFSIZE);
            hist_count = 1;
            hist_write = 1;
        } else {
            hist_count = 0;
            hist_write = 0;
        }
    }
    hist_nav = -1;
}

void ConezShell::historyFree(void)
{
    if (!hist_addr) return;
    // Save most recent entry to DRAM fallback before freeing
    if (hist_count > 0) {
        int last = (hist_write - 1 + HIST_MAX) % HIST_MAX;
        psram_read(hist_addr + last * SHELL_BUFSIZE,
                   (uint8_t *)history, SHELL_BUFSIZE);
        history[SHELL_BUFSIZE - 1] = '\0';
    }
    psram_free(hist_addr);
    hist_addr = 0;
    hist_count = 0;
    hist_write = 0;
    hist_nav = -1;
}

void ConezShell::historyAdd(const char *cmd)
{
    if (!cmd || cmd[0] == '\0') return;

    // Always update DRAM fallback
    strncpy(history, cmd, SHELL_BUFSIZE);
    history[SHELL_BUFSIZE - 1] = '\0';

    if (hist_addr) {
        // Skip duplicate of most recent entry
        if (hist_count > 0) {
            int last = (hist_write - 1 + HIST_MAX) % HIST_MAX;
            char prev[SHELL_BUFSIZE];
            psram_read(hist_addr + last * SHELL_BUFSIZE,
                       (uint8_t *)prev, SHELL_BUFSIZE);
            if (strncmp(prev, cmd, SHELL_BUFSIZE) == 0)
                return;
        }

        // Write to ring
        char buf[SHELL_BUFSIZE];
        memset(buf, 0, SHELL_BUFSIZE);
        strncpy(buf, cmd, SHELL_BUFSIZE - 1);
        psram_write(hist_addr + hist_write * SHELL_BUFSIZE,
                    (const uint8_t *)buf, SHELL_BUFSIZE);
        hist_write = (hist_write + 1) % HIST_MAX;
        if (hist_count < HIST_MAX) hist_count++;
    }

    hist_nav = -1;
}

// Get history entry by offset (0 = most recent, 1 = previous, ...)
// Returns false if offset is out of range.
bool ConezShell::historyGet(int offset, char *buf)
{
    if (hist_addr && hist_count > 0) {
        if (offset < 0 || offset >= hist_count) return false;
        int idx = (hist_write - 1 - offset + HIST_MAX * 2) % HIST_MAX;
        psram_read(hist_addr + idx * SHELL_BUFSIZE,
                   (uint8_t *)buf, SHELL_BUFSIZE);
        buf[SHELL_BUFSIZE - 1] = '\0';
        return true;
    }

    // DRAM fallback: only offset 0 is valid
    if (offset == 0 && history[0] != '\0') {
        strncpy(buf, history, SHELL_BUFSIZE);
        buf[SHELL_BUFSIZE - 1] = '\0';
        return true;
    }
    return false;
}

int ConezShell::printHistory(int /*argc*/, char ** /*argv*/)
{
    if (theShell.hist_addr && theShell.hist_count > 0) {
        char buf[SHELL_BUFSIZE];
        getLock();
        for (int i = theShell.hist_count - 1; i >= 0; i--) {
            if (theShell.historyGet(i, buf))
                shell.printf("  %2d  %s\n", theShell.hist_count - i, buf);
        }
        releaseLock();
    } else if (theShell.history[0] != '\0') {
        getLock();
        shell.print(F("  1  "));
        shell.println(theShell.history);
        releaseLock();
    } else {
        getLock();
        shell.println(F("(no history)"));
        releaseLock();
    }
    return 0;
}

///////////////////////////////////////////////////////////////
// i/o stream indirection/delegation
//
// NOTE: These methods do NOT acquire the print mutex.  Callers that need
// atomic multi-byte output (printHelp, printHistory, printfnl, etc.)
// must hold the lock themselves.  Per-byte locking here would deadlock
// when called from code that already holds the mutex.

size_t ConezShell::write(uint8_t aByte)
{
    if (!shellConnection) return 0;
    return shellConnection->write(aByte);
}

int ConezShell::available()
{
    if (!shellConnection) return 0;
    return shellConnection->available();
}

int ConezShell::read()
{
    if (!shellConnection) return 0;
    return shellConnection->read();
}

int ConezShell::peek()
{
    if (!shellConnection) return 0;
    return shellConnection->peek();
}

void ConezShell::flush()
{
    if (shellConnection) shellConnection->flush();
}

void ConezShell::setTokenizer(TokenizerFunction f)
{
    tokenizer = f;
}
