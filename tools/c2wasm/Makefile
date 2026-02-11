CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
TARGET   = c2wasm

all: $(TARGET)

$(TARGET): c2wasm.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)

.PHONY: all clean
