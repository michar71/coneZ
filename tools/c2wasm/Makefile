CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
TARGET   = c2wasm

BUILDNUM_FILE = ../buildnum.txt
BUILDNUM = $(shell cat $(BUILDNUM_FILE) 2>/dev/null || echo 0)
NEXT_BUILDNUM = $(shell echo $$(($(BUILDNUM) + 1)))

all: $(TARGET)

$(TARGET): c2wasm.c .buildnum
	$(CC) $(CFLAGS) -DBUILD_NUMBER=$(NEXT_BUILDNUM) -o $@ c2wasm.c
	@echo $(NEXT_BUILDNUM) > $(BUILDNUM_FILE)

.buildnum:
.PHONY: all clean .buildnum

clean:
	rm -f $(TARGET)
