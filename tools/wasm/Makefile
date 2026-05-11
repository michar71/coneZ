CC      = clang
STRIP   = $(or $(shell which llvm-strip 2>/dev/null),$(shell which wasm-strip 2>/dev/null),$(shell ls /usr/lib/llvm-*/bin/llvm-strip 2>/dev/null | tail -1))
CFLAGS  = --target=wasm32 -mbulk-memory -O2 -nostdlib -I .
LDFLAGS = -Wl,--no-entry -Wl,--export=setup -Wl,--export=loop -Wl,--allow-undefined \
          -Wl,-z,stack-size=256

EXAMPLES = examples/rgb_cycle.wasm examples/hsv_rainbow.wasm examples/sos_flash.wasm

all: $(EXAMPLES)

examples/%.wasm: examples/%.c conez_api.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
	$(STRIP) $@

# Copy built modules to firmware/data/ for LittleFS upload
install: $(EXAMPLES)
	cp $(EXAMPLES) ../../firmware/data/

clean:
	rm -f examples/*.wasm

.PHONY: all install clean
