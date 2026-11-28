CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
TARGET   = bas2wasm
SRCS     = main.c buf.c imports.c lexer.c expr.c stmt.c assemble.c
OBJS     = $(SRCS:.c=.o)

BUILDNUM_FILE = buildnum.txt
BUILDNUM = $(shell cat $(BUILDNUM_FILE) 2>/dev/null || echo 0)
NEXT_BUILDNUM = $(shell echo $$(($(BUILDNUM) + 1)))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm
	@echo $(NEXT_BUILDNUM) > $(BUILDNUM_FILE)

main.o: main.c bas2wasm.h .buildnum
	$(CC) $(CFLAGS) -DBUILD_NUMBER=$(NEXT_BUILDNUM) -c -o $@ $<

.buildnum:
.PHONY: all clean test test-runtime test-host test-sim verify .buildnum

test: $(TARGET)
	@test/run_tests.sh

# Execution-based tests: compile each .bas file with an `' EXPECTED:` block,
# run it under node's WebAssembly host with stub imports (FORMAT %/&/$
# interpretation + basic_str_* string pool), diff captured output.
# Catches semantic regressions that wasm-validate can't.
test-runtime: $(TARGET)
	@command -v node >/dev/null 2>&1 || { echo "node required for test-runtime"; exit 1; }
	@node test/run_runtime.js

# The same tests on REAL host code instead of run_runtime.js's JavaScript
# imports: tools/fwhost is the firmware's own wasm3 fork, run sequence and
# string / FORMAT / print / file / math imports, compiled for this machine
# with sanitizers, in both memory configurations (plain RAM = Heltec board,
# PSRAM split = ConeZ board). A bug in the firmware's imports fails here; it
# cannot fail test-runtime. Needs node and a C++ compiler.
test-host: $(TARGET)
	@command -v node >/dev/null 2>&1 || { echo "node required for test-host"; exit 1; }
	@$(MAKE) --no-print-directory -C ../fwhost
	@node test/run_runtime.js --host ../fwhost/fwhost-dram
	@node test/run_runtime.js --host ../fwhost/fwhost-psram

# ...and on the simulator's host code (its headless runner, built with the
# simulator: make -C ../../simulator/conez/build conez-wasm-run).
SIM_RUN ?= ../../simulator/conez/build/conez-wasm-run
test-sim: $(TARGET)
	@command -v node >/dev/null 2>&1 || { echo "node required for test-sim"; exit 1; }
	@test -x $(SIM_RUN) || { echo "$(SIM_RUN) not built -- build the simulator first (see documentation/simulator.txt)"; exit 1; }
	@node test/run_runtime.js --host $(SIM_RUN)

%.o: %.c bas2wasm.h
	$(CC) $(CFLAGS) -c -o $@ $<

verify: $(TARGET)
	@fail=0; \
	for f in test/*.bas; do \
		base=$$(basename "$$f" .bas); \
		case "$$base" in reject_*) continue;; esac; \
		./bas2wasm "$$f" -o "/tmp/bw_verify_$$base.wasm" 2>/dev/null; \
		if cmp -s "/tmp/bw_verify_$$base.wasm" "test/baseline/$$base.wasm"; then \
			echo "  OK  $$base"; \
		else \
			echo "  FAIL $$base"; fail=1; \
		fi; \
		rm -f "/tmp/bw_verify_$$base.wasm"; \
	done; \
	if [ $$fail -eq 1 ]; then echo "VERIFY FAILED"; exit 1; fi; \
	echo "All baselines match."

clean:
	rm -f $(TARGET) $(OBJS)
