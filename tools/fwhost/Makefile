# fwhost -- the firmware's WASM host code, built to run on this machine.
# See README.txt.

FW       = ../../firmware
WASM3    = $(FW)/lib/wasm3/src
FWWASM   = $(FW)/src/wasm

CC      ?= cc
CXX     ?= c++
OPT     ?= -O1 -g
# Sanitizers on by default: the point is to catch the host code misbehaving.
# `make SAN=` builds without them.
SAN     ?= -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer

DEFS     = -DINCLUDE_WASM
INCS     = -Ishim -I$(WASM3) -I$(FWWASM) -I$(FW)/src/util
# wasm3 reads 64-bit constants from 4-byte-aligned code pages (m3_compile.c).
# That is fine on the device (and here); it is upstream wasm3, not ours, so the
# alignment check is off for the wasm3 sources only.
CFLAGS_  = $(OPT) $(SAN) -fno-sanitize=alignment $(DEFS) $(INCS) -w
CXXFLAGS_= $(OPT) $(SAN) $(DEFS) $(INCS) -std=gnu++20 -Wall -Wno-unused-function -Wno-unused-variable

WASM3_SRCS = $(wildcard $(WASM3)/*.c)
# The firmware's own files, compiled unchanged.
FW_SRCS  = $(FWWASM)/wasm_psram_glue.cpp \
           $(FWWASM)/wasm_imports_string.cpp \
           $(FWWASM)/wasm_format.cpp \
           $(FWWASM)/wasm_imports_io.cpp \
           $(FWWASM)/wasm_imports_file.cpp \
           $(FWWASM)/wasm_imports_math.cpp \
           $(FWWASM)/wasm_imports_system.cpp \
           $(FW)/src/util/curve.cpp
# wasm_wrapper.cpp is #included by fwhost.cpp (wasm_run() is static).
DEPS     = fwhost.cpp $(FWWASM)/wasm_wrapper.cpp $(FW_SRCS) $(WASM3_SRCS) \
           $(wildcard $(WASM3)/*.h) $(wildcard $(FWWASM)/*.h) $(wildcard shim/*.h shim/*/*.h) Makefile

all: fwhost-dram fwhost-psram

# $(1) = variant name, $(2) = d_m3UsePsramMemory. One compiler call per file;
# the whole thing takes a few seconds.
define BUILD
	@mkdir -p obj/$(1)
	@for f in $(WASM3_SRCS); do \
		$(CC) $(CFLAGS_) -Dd_m3UsePsramMemory=$(2) -c $$f -o obj/$(1)/$$(basename $$f .c).o || exit 1; \
	done
	@for f in fwhost.cpp $(FW_SRCS); do \
		$(CXX) $(CXXFLAGS_) -Dd_m3UsePsramMemory=$(2) -c $$f -o obj/$(1)/$$(basename $$f .cpp).o || exit 1; \
	done
	$(CXX) $(SAN) -o $@ obj/$(1)/*.o -lm
endef

fwhost-dram: $(DEPS)
	$(call BUILD,dram,0)

fwhost-psram: $(DEPS)
	$(call BUILD,psram,1)

clean:
	rm -f fwhost-dram fwhost-psram obj/dram/*.o obj/psram/*.o

.PHONY: all clean
