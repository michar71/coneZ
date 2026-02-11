CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
TARGET   = c2wasm

BUILDNUM_FILE = ../buildnum.txt
BUILDNUM = $(shell cat $(BUILDNUM_FILE) 2>/dev/null || echo 0)
NEXT_BUILDNUM = $(shell echo $$(($(BUILDNUM) + 1)))

SRCS = main.c lexer.c preproc.c type.c expr.c stmt.c assemble.c buf.c imports.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS) .buildnum
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm
	@echo $(NEXT_BUILDNUM) > $(BUILDNUM_FILE)

%.o: %.c c2wasm.h
	$(CC) $(CFLAGS) -DBUILD_NUMBER=$(NEXT_BUILDNUM) -c -o $@ $<

.buildnum:
.PHONY: all clean test .buildnum

test: $(TARGET)
	@test/run_tests.sh

clean:
	rm -f $(TARGET) $(OBJS)
