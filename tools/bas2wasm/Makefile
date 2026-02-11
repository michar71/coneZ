CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
TARGET   = bas2wasm
SRCS     = main.c buf.c imports.c lexer.c expr.c stmt.c assemble.c
OBJS     = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm

%.o: %.c bas2wasm.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
