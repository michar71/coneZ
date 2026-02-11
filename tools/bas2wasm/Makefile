CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
TARGET   = bas2wasm
SRCS     = main.c buf.c imports.c lexer.c expr.c stmt.c assemble.c
OBJS     = $(SRCS:.c=.o)

BUILDNUM_FILE = ../buildnum.txt
BUILDNUM = $(shell cat $(BUILDNUM_FILE) 2>/dev/null || echo 0)
NEXT_BUILDNUM = $(shell echo $$(($(BUILDNUM) + 1)))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm
	@echo $(NEXT_BUILDNUM) > $(BUILDNUM_FILE)

main.o: main.c bas2wasm.h .buildnum
	$(CC) $(CFLAGS) -DBUILD_NUMBER=$(NEXT_BUILDNUM) -c -o $@ $<

.buildnum:
.PHONY: all clean .buildnum

%.o: %.c bas2wasm.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)
