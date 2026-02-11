CC      = clang
CFLAGS  = --target=wasm32 -mbulk-memory -O2 -nostdlib -I .
LDFLAGS = -Wl,--no-entry -Wl,--export=setup -Wl,--export=loop -Wl,--allow-undefined

C_EXAMPLES    = examples/rgb_cycle.wasm examples/hsv_rainbow.wasm
RUST_EXAMPLES = examples/rust_rainbow.wasm examples/rust_rgb_cycle.wasm
EXAMPLES      = $(C_EXAMPLES) $(RUST_EXAMPLES)

all: $(EXAMPLES)

examples/%.wasm: examples/%.c conez_api.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

examples/rust_rainbow.wasm: examples/rust_rainbow/src/lib.rs examples/rust_rainbow/Cargo.toml
	cd examples/rust_rainbow && cargo build --release --target wasm32-wasip1
	wasm-opt --enable-bulk-memory -Oz \
		examples/rust_rainbow/target/wasm32-wasip1/release/rust_rainbow.wasm \
		-o $@

examples/rust_rgb_cycle.wasm: examples/rust_rgb_cycle/src/lib.rs examples/rust_rgb_cycle/Cargo.toml
	cd examples/rust_rgb_cycle && cargo build --release --target wasm32-unknown-unknown
	wasm-opt --enable-bulk-memory -Oz \
		examples/rust_rgb_cycle/target/wasm32-unknown-unknown/release/rust_rgb_cycle.wasm \
		-o $@

# Copy built modules to firmware/data/ for LittleFS upload
install: $(EXAMPLES)
	cp $(EXAMPLES) ../../firmware/data/

clean:
	rm -f examples/*.wasm
	cd examples/rust_rainbow && cargo clean
	cd examples/rust_rgb_cycle && cargo clean

.PHONY: all install clean
