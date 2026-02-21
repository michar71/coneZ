#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "shell.h"
#include "main.h"
#include "printManager.h"
#include "psram.h"
#include "telnet.h"

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
// Quote-aware tokenizer (drop-in replacement for strtok_r).
// Handles "..." grouping and \" / \\ escapes both inside and outside quotes.
// Modifies the input buffer in-place (like strtok_r).
static char *quote_tokenizer(char *str, const char *delim, char **saveptr)
{
    char *p = str ? str : *saveptr;
    if (!p) return NULL;

    // Skip leading delimiters
    while (*p && strchr(delim, *p)) p++;
    if (!*p) { *saveptr = p; return NULL; }

    char *token = p;
    char *w = p;  // write pointer for in-place escape/quote removal

    while (*p && !strchr(delim, *p)) {
        if (*p == '"') {
            // Quoted section — consume until closing quote
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) {
                    *w++ = p[1];
                    p += 2;
                } else {
                    *w++ = *p++;
                }
            }
            if (*p == '"') p++;  // skip closing quote
        } else if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) {
            // Escaped quote or backslash outside quotes
            *w++ = p[1];
            p += 2;
        } else {
            *w++ = *p++;
        }
    }

    if (*p) p++;  // skip trailing delimiter
    *w = '\0';
    *saveptr = p;
    return token;
}

////////////////////////////////////////////////////////////////////////////////
// CLI command shell — based on ConezShell by Phil Jansen,
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
        Command(const char * n, CommandFunction f,
                const char *fs = NULL, const char * const *sc = NULL,
                TabCompleteFunc tcf = NULL, bool va = false):
            nameAndDocs(n), myFunc(f), fileSpec(fs), subcommands(sc),
            tabCompleteFunc(tcf), valArgs(va) {};

        int execute(int argc, char **argv)
        {
            return myFunc(argc, argv);
        };

        // Comparison used for sort commands
        int compare(const Command * other) const
        {
            return compareName(other->nameAndDocs);
        };

        int compareName(const char * aName) const
        {
            // Find length of command name (up to first space)
            const char *name = nameAndDocs;
            const char *sp = strchr(name, ' ');
            int nameLen = sp ? (int)(sp - name) : (int)strlen(name);
            // Compare only the name portion
            const char *asp = strchr(aName, ' ');
            int aLen = asp ? (int)(asp - aName) : (int)strlen(aName);
            int cmpLen = nameLen > aLen ? nameLen : aLen;
            return strncasecmp(name, aName, cmpLen);
        };

        void renderDocumentation(ConezStream& str) const
        {
            getLock();
            str.print("  ");
            str.print(nameAndDocs);
            str.println();
            releaseLock();
        }

        // Extract just the command name (before space) into buf.
        // Returns length of name (truncated to bufsz-1).
        int getName(char *buf, int bufsz) const
        {
            const char *p = nameAndDocs;
            int i = 0;
            while (i < bufsz - 1) {
                char ch = p[i];
                if (ch == '\0' || ch == ' ') break;
                buf[i++] = ch;
            }
            buf[i] = '\0';
            return i;
        };

        Command * next;

    private:

        const char * const nameAndDocs;
        const CommandFunction myFunc;
    public:
        const char * const fileSpec;           // NULL = no files, "*" = all, "*.bas;*.c" = filtered
        const char * const *subcommands;      // NULL-terminated list, or NULL
        const TabCompleteFunc tabCompleteFunc; // multi-level completion callback, or NULL
        const bool valArgs;                   // true = args are typed values, show <val>
};

////////////////////////////////////////////////////////////////////////////////
ConezShell::ConezShell()
    : shellConnection(NULL),
      m_lastErrNo(EXIT_SUCCESS),
      tokenizer(quote_tokenizer)
{
    resetBuffer();
    history[0] = '\0';
    hist_addr = 0;
    hist_count = 0;
    hist_write = 0;
    hist_nav = -1;
    inputActive = false;


    addCommand("history", ConezShell::printHistory);
};

//////////////////////////////////////////////////////////////////////////////
void ConezShell::addCommand(
    const char * name, CommandFunction f,
    const char *fileSpec, const char * const *subcommands,
    TabCompleteFunc tabCompleteFunc, bool valArgs)
{
    auto * newCmd = new Command(name, f, fileSpec, subcommands, tabCompleteFunc, valArgs);

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
    // Send prompt + current input to newly-connected telnet clients
    if (inputActive && telnet.hasNewClients()) {
        getLock();
        telnet.sendToNew(sh_prompt());
        if (inptr > 0)
            telnet.sendToNew((const uint8_t *)linebuffer, inptr);
        telnet.clearNewClients();
        releaseLock();
    }

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
        // Log the command to debug sinks (MQTT, file log) — not to console
        if (pending[0] != '\0')
            printfnl(SOURCE_COMMANDS_PROMPT, "> %s\n", pending);
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
void ConezShell::attach(ConezStream & requester)
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
// USB/Telnet objects are never accessed concurrently from two tasks,
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
                            shellConnection->print("\033[C");
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
                            shellConnection->print("\033[D");
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
                                shellConnection->print("\033[C");
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
                            shellConnection->print("\b \b");
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
                            shellConnection->print("\033[C");
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
                    shellConnection->print("\r\n");
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
                    shellConnection->print("\n");
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

    return report("Too many arguments to parse", -1);
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
        shellConnection->print("\"");
        shellConnection->print(argv[0]);
        shellConnection->print("\": ");
        releaseLock();
    }

    return report("command not found", -1);
}

//////////////////////////////////////////////////////////////////////////////
int ConezShell::lastErrNo(void)
{
    return m_lastErrNo;
}

//////////////////////////////////////////////////////////////////////////////
int ConezShell::report(const char * constMsg, int errorCode)
{
    if (errorCode != EXIT_SUCCESS)
    {
        const char *message = (const char *)constMsg;
        if (shellConnection)
        {
            getLock();
            shellConnection->print(errorCode);
            if (message[0] != '\0') {
                shellConnection->print(": ");
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
void ConezShell::suspendLine(ConezStream *out)
{
    if (!inputActive || !out) return;
    if (getAnsiEnabled()) {
        out->print(sh_reset());
        out->print("\r\033[K");
    } else {
        out->print("\r");
        for (int i = 0; i < inptr + 2; i++) out->write(' ');
        out->print("\r");
    }
}

// Called by printManager under mutex — redraw the prompt+input line
void ConezShell::resumeLine(ConezStream *out)
{
    if (!inputActive || !out) return;
    out->print(sh_prompt());
    out->write((const uint8_t*)linebuffer, inptr);
    // reposition cursor if not at end
    for (int i = cursor; i < inptr; i++) out->write('\b');
}

// Map TAB_COMPLETE_VALUE_* sentinels to display labels.
// Returns NULL if result is not a value sentinel.
static const char *val_sentinel_label(const char * const *result)
{
    if (result == TAB_COMPLETE_VALUE)       return "<val>";
    if (result == TAB_COMPLETE_VALUE_STR)   return "<string>";
    if (result == TAB_COMPLETE_VALUE_INT)   return "<int>";
    if (result == TAB_COMPLETE_VALUE_FLOAT) return "<float>";
    if (result == TAB_COMPLETE_VALUE_HEX)   return "<hex>";
    return NULL;
}

// Check if filename matches a fileSpec pattern ("*", "*.bas;*.c", "/" = dirs only).
// Directories always match unless spec is "/" (dirs only).
static bool matchesFileSpec(const char *fname, int flen, bool isDir, const char *spec)
{
    if (spec && spec[0] == '/' && spec[1] == '\0') return isDir;
    if (isDir || !spec || (spec[0] == '*' && spec[1] == '\0')) return true;
    // Walk semicolon-separated "*.ext" patterns
    const char *p = spec;
    while (*p) {
        // Skip leading "*"
        const char *ext = p;
        if (*ext == '*') ext++;
        // Find end of this pattern
        const char *semi = ext;
        while (*semi && *semi != ';') semi++;
        int extLen = semi - ext;
        // Check if filename ends with this suffix
        if (extLen > 0 && flen >= extLen &&
            strncasecmp(fname + flen - extLen, ext, extLen) == 0)
            return true;
        p = *semi ? semi + 1 : semi;
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////////
// Tab completion for commands (first word) and filenames (after space).
// Called WITHOUT print_mutex held — acquires it internally as needed.
void ConezShell::tabComplete(void)
{
    if (!shellConnection) return;

    // Determine which word we're completing (0 = command, 1 = first arg, 2+ = later args)
    // Quote-aware: "foo bar" counts as one word, not two.
    int wordStart = 0;
    int wordIndex = 0;
    bool inSpace = false;
    bool inQuote = false;
    for (int i = 0; i < cursor; i++) {
        char ch = linebuffer[i];
        if (inQuote) {
            if (ch == '\\' && i + 1 < cursor && (linebuffer[i+1] == '"' || linebuffer[i+1] == '\\'))
                i++;  // skip escaped char
            else if (ch == '"')
                inQuote = false;
        } else if (ch == '"') {
            inQuote = true;
            inSpace = false;
        } else if (ch == ' ') {
            if (!inSpace) { wordIndex++; inSpace = true; }
            wordStart = i + 1;
        } else {
            inSpace = false;
        }
    }

    // For arguments (after first word), look up the command to decide
    // whether to complete filenames, subcommands, or nothing.
    const char * const *subcmds = NULL;
    bool doFileComplete = false;
    const char *activeFileSpec = NULL;  // fileSpec for filtering (NULL = no files)
    bool isSubcmdResult = false;  // true when showing subcommand-style list (show <cr>)
    bool showValHint = false;     // true when valArgs command at word 2+ should show <val>
    if (wordIndex > 0) {
        char firstWord[32];
        int fw = 0;
        while (fw < cursor && fw < (int)sizeof(firstWord) - 1 && linebuffer[fw] != ' ')
            firstWord[fw] = linebuffer[fw], fw++;
        firstWord[fw] = '\0';

        Command *foundCmd = NULL;
        for (Command *cmd = firstCommand; cmd; cmd = cmd->next) {
            if (cmd->compareName(firstWord) == 0) { foundCmd = cmd; break; }
        }
        if (!foundCmd) return;

        if (foundCmd->tabCompleteFunc) {
            // Parse words from linebuffer for callback (quote-aware)
            const char * const *result;
            {
                char lbcopy[SHELL_BUFSIZE];
                memcpy(lbcopy, linebuffer, cursor);
                lbcopy[cursor] = '\0';
                const char *words[10];
                int nWords = 0;
                char *rest = NULL;
                char *tok = quote_tokenizer(lbcopy, " \t", &rest);
                while (tok && nWords < 10) {
                    words[nWords++] = tok;
                    tok = quote_tokenizer(NULL, " \t", &rest);
                }
                result = foundCmd->tabCompleteFunc(wordIndex, words, nWords);
            }
            if (result == TAB_COMPLETE_FILES) {
                doFileComplete = true;
                activeFileSpec = foundCmd->fileSpec ? foundCmd->fileSpec : "*";
            } else if (val_sentinel_label(result)) {
                getLock();
                shellConnection->print(sh_reset());
                shellConnection->print("\r\n");
                shellConnection->print(val_sentinel_label(result));
                shellConnection->print("\r\n");
                shellConnection->print(sh_prompt());
                shellConnection->write((const uint8_t*)linebuffer, inptr);
                for (int i = cursor; i < inptr; i++) shellConnection->write('\b');
                releaseLock();

                return;
            } else if (result) {
                subcmds = result;
                isSubcmdResult = true;
            } else {
                // Callback says no completion — show <cr>
                getLock();
                shellConnection->print(sh_reset());
                shellConnection->print("\r\n<cr>\r\n");
                shellConnection->print(sh_prompt());
                shellConnection->write((const uint8_t*)linebuffer, inptr);
                for (int i = cursor; i < inptr; i++) shellConnection->write('\b');
                releaseLock();
                return;
            }
        } else {
            doFileComplete = (foundCmd->fileSpec != NULL);
            activeFileSpec = foundCmd->fileSpec;
            if (wordIndex == 1) {
                subcmds = foundCmd->subcommands;
            }
            if (foundCmd->subcommands) {
                isSubcmdResult = true;
                if (wordIndex > 1 && foundCmd->valArgs)
                    showValHint = true;
            }
            if (!doFileComplete && !isSubcmdResult) {
                getLock();
                shellConnection->print(sh_reset());
                // Show <val> if command expects typed values, otherwise <cr>
                shellConnection->print(foundCmd->valArgs ? "\r\n<val>\r\n" : "\r\n<cr>\r\n");
                shellConnection->print(sh_prompt());
                shellConnection->write((const uint8_t*)linebuffer, inptr);
                for (int i = cursor; i < inptr; i++) shellConnection->write('\b');
                releaseLock();
                return;
            }
        }
    }

    // Auto-insert '/' at wordStart for file completion when prefix doesn't start with '/'
    if (doFileComplete && (cursor == wordStart || linebuffer[wordStart] != '/') &&
        inptr < SHELL_BUFSIZE - 1) {
        getLock();
        memmove(&linebuffer[wordStart + 1], &linebuffer[wordStart], inptr - wordStart);
        linebuffer[wordStart] = '/';
        inptr++;
        cursor++;
        linebuffer[inptr] = '\0';
        releaseLock();
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

    if (wordIndex == 0) {
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
    } else if (subcmds) {
        // Subcommand completion — walk NULL-terminated list
        for (int i = 0; subcmds[i] && nMatches < MAX_MATCHES; i++) {
            int slen = strlen(subcmds[i]);
            if (slen >= prefixLen && slen < MAX_NAME &&
                strncasecmp(subcmds[i], prefix, prefixLen) == 0) {
                memcpy(matches[nMatches].name, subcmds[i], slen + 1);
                matches[nMatches].len = slen;
                matches[nMatches].isDir = false;
                nMatches++;
            }
        }
    } else if (doFileComplete) {
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        getLock();
        char dpath[128];
        lfs_path(dpath, sizeof(dpath), dirPath);
        DIR *dir = opendir(dpath);
        if (dir) {
            struct dirent *ent;
            while ((ent = readdir(dir)) && nMatches < MAX_MATCHES) {
                const char *fname = ent->d_name;
                int flen = strlen(fname);
                char fullpath[192];
                snprintf(fullpath, sizeof(fullpath), "%s/%s", dpath, fname);
                struct stat st;
                bool isDir = (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode));
                if (flen >= partialLen && flen < MAX_NAME &&
                    strncasecmp(fname, partial, partialLen) == 0 &&
                    matchesFileSpec(fname, flen, isDir, activeFileSpec)) {
                    memcpy(matches[nMatches].name, fname, flen + 1);
                    matches[nMatches].len = flen;
                    matches[nMatches].isDir = isDir;
                    nMatches++;
                }
            }
            closedir(dir);
        }
        releaseLock();
#pragma GCC diagnostic pop
    }

    if (nMatches == 0) {
        // Empty subcommand list + empty prefix = command is complete
        if (isSubcmdResult && prefixLen == 0) {
            getLock();
            shellConnection->print(sh_reset());
            shellConnection->print(showValHint ? "\r\n<val>\r\n" : "\r\n<cr>\r\n");
            shellConnection->print(sh_prompt());
            shellConnection->write((const uint8_t*)linebuffer, inptr);
            for (int i = cursor; i < inptr; i++) shellConnection->write('\b');
            releaseLock();
        }

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
    if (wordIndex > 0) {
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

        // If single match, append suffix (skip if match ends with '.' — it's a prefix)
        if (nMatches == 1 && matches[0].name[matches[0].len - 1] != '.') {
            char suffix = wordIndex > 0 ? (matches[0].isDir ? '/' : ' ') : ' ';
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
    
    } else if (nMatches == 1) {
        // Exact match, append suffix (skip if match ends with '.' — it's a prefix)
        if (matches[0].name[matches[0].len - 1] != '.') {
            getLock();
            int prevLen = inptr;
            char suffix = wordIndex > 0 ? (matches[0].isDir ? '/' : ' ') : ' ';
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
        }
    
    } else {
        // List all matches immediately
        getLock();
        shellConnection->print(sh_reset());
        shellConnection->print("\r\n");
        // If completing subcommands, show [cr] to indicate bare command is also valid
        if (isSubcmdResult) shellConnection->print("[cr]  ");
        for (int i = 0; i < nMatches; i++) {
            shellConnection->print(matches[i].name);
            if (matches[i].isDir) shellConnection->write('/');
            shellConnection->print("  ");
        }
        shellConnection->print("\r\n");
        // Re-display prompt + current line
        shellConnection->print(sh_prompt());
        shellConnection->write((const uint8_t*)linebuffer, inptr);
        for (int i = cursor; i < inptr; i++) shellConnection->write('\b');
        releaseLock();
    
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
        shell.print("  1  ");
        shell.println(theShell.history);
        releaseLock();
    } else {
        getLock();
        shell.println("(no history)");
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
