
#ifndef SHELL_H
#define SHELL_H

#include "conez_stream.h"

#ifndef SHELL_BUFSIZE
#define SHELL_BUFSIZE 88
#endif

// Sentinel return value from TabCompleteFunc meaning "complete filenames"
#define TAB_COMPLETE_FILES ((const char * const *)1)

// Sentinel return values from TabCompleteFunc for typed value indicators
#define TAB_COMPLETE_VALUE       ((const char * const *)2)  // <val>
#define TAB_COMPLETE_VALUE_STR   ((const char * const *)3)  // <string>
#define TAB_COMPLETE_VALUE_INT   ((const char * const *)4)  // <int>
#define TAB_COMPLETE_VALUE_FLOAT ((const char * const *)5)  // <float>
#define TAB_COMPLETE_VALUE_HEX   ((const char * const *)6)  // <hex>

// Callback for multi-level tab completion.
// wordIndex: word being completed (1 = first arg, 2 = second, ...)
// words[0..nWords-1]: complete words typed before cursor (words[0] = command)
// Returns: NULL-terminated string array, NULL (no completion),
//          TAB_COMPLETE_FILES, or TAB_COMPLETE_VALUE
typedef const char * const * (*TabCompleteFunc)(int wordIndex, const char **words, int nWords);

////////////////////////////////////////////////////////////////////////////////
// CLI command shell — based on ConezShell by Phil Jansen,
// heavily modified for ConeZ (cursor editing, history, suspend/resume).

class ConezShell : public ConezStream {
    public:

        // The singleton instance of the shell
        static ConezShell theShell;

        // Unix-style (from 1970!)
        // functions must have a signature like: "int hello(int argc, char ** argv)"
        typedef int (*CommandFunction)(int, char ** );

        /**
         * @brief Registers a command with the shell processor.
         *
         * @param name Command name, with optional documentation.  The
         *   command must be delimited from the rest of the documentation
         *   using a space, which implies that the command itself cannot
         *   contain space characters.  Anything after the initial space
         *   delimiter will be treated as documentation for display in
         *   the help message.
         * @param f The command function that will be called when the command
         *   is entered into the shell.
         */
        void addCommand(const char * name, CommandFunction f,
                        const char *fileSpec = NULL,
                        const char * const *subcommands = NULL,
                        TabCompleteFunc tabCompleteFunc = NULL,
                        bool valArgs = false);

        void attach(ConezStream & shellSource);

        // Print the initial prompt (call once after attach + addCommand)
        void showPrompt(void);

        void setEcho(bool echo);

        // check for a complete command and run it if available
        // non blocking
        bool executeIfInput(void);  // returns true when command attempted
        int lastErrNo(void);

        int execute( const char aCommandString[]);  // shell.execute("echo hello world");

        static int printHistory(int argc, char **argv);

        // PSRAM-backed history ring buffer (falls back to single DRAM entry)
        void historyInit(void);     // allocate PSRAM ring buffer
        void historyFree(void);     // free PSRAM ring, keep DRAM fallback

        void resetBuffer(void);

        // Called by printManager (under mutex) to erase/redraw the input line
        // around background output. Caller must already hold print_mutex.
        void suspendLine(ConezStream *out);
        void resumeLine(ConezStream *out);

        // this shell delegates communication to/from the attached stream
        // (which sent the command)
        // Note changing streams may intermix serial data
        //
        virtual size_t write(uint8_t);
        virtual int available();
        virtual int read();
        virtual int peek();
        virtual void flush(); // esp32 needs an implementation

        // The function signature of the tokening function.  This is based
        // on the parameters of the strtok_r(3) function.
        typedef char* (*TokenizerFunction)(char* str, const char* delim, char** saveptr);

        // Call this to change the tokenizer function used internally.  By
        // default strtok_r() is used, so the use of this function is
        // optional.
        void setTokenizer(TokenizerFunction f);

    private:

        ConezShell(void);

        ConezStream * shellConnection;
        int m_lastErrNo;
        int execute(void);
        int execute(int argc, char** argv);

        bool prepInput(void);

        int report(const char * message, int errorCode);
        static const char MAXARGS = 10;
        char linebuffer[SHELL_BUFSIZE];
        int inptr;
        int cursor;         // cursor position within linebuffer (0..inptr)
        int escState;       // escape sequence state: 0=normal, 1=got ESC, 2=got ESC[, 3=got ESC[3
        // History ring buffer (PSRAM-backed when available, else single DRAM entry)
        static const int HIST_MAX = 32;
        uint32_t hist_addr;         // PSRAM address of ring buffer (0 = not allocated)
        int hist_count;             // number of valid entries in ring (0..HIST_MAX)
        int hist_write;             // next write slot in ring (0..HIST_MAX-1)
        int hist_nav;               // navigation offset (-1 = not navigating)
        char history[SHELL_BUFSIZE]; // DRAM fallback (single entry, always available)

        void historyAdd(const char *cmd);
        bool historyGet(int offset, char *buf);  // offset: 0=most recent, 1=previous...

        bool inputActive;   // true when prompt is visible and user may be typing
        void redrawLine(int prevLen);  // redraw line after mid-line edit
        void tabComplete(void);        // tab completion for commands and filenames

        class Command;
        static Command * firstCommand;
        bool echoEnabled = true;

        TokenizerFunction tokenizer;
};

////////////////////////////////////////////////////////////////////////////////
extern ConezShell& shell;

#endif /* SHELL_H */
