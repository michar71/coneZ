CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
TARGET   = c2wasm

BUILDNUM_FILE = ../buildnum.txt
BUILDNUM = $(shell cat $(BUILDNUM_FILE) 2>/dev/null || echo 0)
NEXT_BUILDNUM = $(shell echo $$(($(BUILDNUM) + 1)))

SRCS = main.c lexer.c preproc.c type.c type_ops.c expr.c stmt.c assemble.c buf.c imports.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS) .buildnum
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm
	@echo $(NEXT_BUILDNUM) > $(BUILDNUM_FILE)

%.o: %.c c2wasm.h c2wasm_platform.h
	$(CC) $(CFLAGS) -DBUILD_NUMBER=$(NEXT_BUILDNUM) -c -o $@ $<

.buildnum:
.PHONY: all clean test verify .buildnum

test: $(TARGET)
	@test/run_tests.sh

verify: $(TARGET)
	@fail=0; \
	for f in test/*.c test/examples/*.c; do \
		[ -f "$$f" ] || continue; \
		base=$$(basename "$$f" .c); \
		case "$$base" in reject_*) continue;; esac; \
		case "$$base" in predefined_macros) continue;; esac; \
		if echo "$$f" | grep -q "examples/"; then \
			out="/tmp/cw_verify_ex_$$base.wasm"; bl="test/baseline/ex_$$base.wasm"; \
		else \
			out="/tmp/cw_verify_$$base.wasm"; bl="test/baseline/$$base.wasm"; \
		fi; \
		./c2wasm "$$f" -o "$$out" 2>/dev/null; \
		if cmp -s "$$out" "$$bl"; then \
			echo "  OK  $$base"; \
		else \
			echo "  FAIL $$base"; fail=1; \
		fi; \
		rm -f "$$out"; \
	done; \
	if [ $$fail -eq 1 ]; then echo "VERIFY FAILED"; exit 1; fi; \
	echo "All baselines match."

clean:
	rm -f $(TARGET) $(OBJS)
