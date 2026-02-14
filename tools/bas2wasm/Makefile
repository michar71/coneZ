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
.PHONY: all clean verify .buildnum

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
