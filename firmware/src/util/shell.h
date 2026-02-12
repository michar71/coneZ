
#ifndef SHELL_H
#define SHELL_H

#ifndef SHELL_BUFSIZE
#define SHELL_BUFSIZE 88
#endif

////////////////////////////////////////////////////////////////////////////////
// Serial command shell — based on ConezShell by Phil Jansen,
// heavily modified for ConeZ (cursor editing, history, suspend/resume).

class ConezShell : public Stream {
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
        void addCommand(const __FlashStringHelper * name, CommandFunction f);

        void attach(Stream & shellSource);

        // Print the initial prompt (call once after attach + addCommand)
        void showPrompt(void);

        void setEcho(bool echo);

        // check for a complete command and run it if available
        // non blocking
        bool executeIfInput(void);  // returns true when command attempted
        int lastErrNo(void);

        int execute( const char aCommandString[]);  // shell.execute("echo hello world");

        static int printHistory(int argc, char **argv);

        void resetBuffer(void);

        // Called by printManager (under mutex) to erase/redraw the input line
        // around background output. Caller must already hold print_mutex.
        void suspendLine(Stream *out);
        void resumeLine(Stream *out);

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

        Stream * shellConnection;
        int m_lastErrNo;
        int execute(void);
        int execute(int argc, char** argv);

        bool prepInput(void);

        int report(const __FlashStringHelper * message, int errorCode);
        static const char MAXARGS = 10;
        char linebuffer[SHELL_BUFSIZE];
        int inptr;
        int cursor;         // cursor position within linebuffer (0..inptr)
        int escState;       // escape sequence state: 0=normal, 1=got ESC, 2=got ESC[, 3=got ESC[3
        char history[SHELL_BUFSIZE]; // single previous-command buffer

        bool inputActive;   // true when prompt is visible and user may be typing

        void redrawLine(int prevLen);  // redraw line after mid-line edit

        class Command;
        static Command * firstCommand;
        bool echoEnabled = true;

        TokenizerFunction tokenizer;
};

////////////////////////////////////////////////////////////////////////////////
extern ConezShell& shell;

#endif /* SHELL_H */
