CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
TARGET   = sewerpipe
SRCS     = main.c mqtt.c broker.c
OBJS     = $(SRCS:.c=.o)

BUILDNUM_FILE = ../buildnum.txt
BUILDNUM = $(shell cat $(BUILDNUM_FILE) 2>/dev/null || echo 0)
NEXT_BUILDNUM = $(shell echo $$(($(BUILDNUM) + 1)))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)
	@echo $(NEXT_BUILDNUM) > $(BUILDNUM_FILE)

main.o: main.c sewerpipe.h .buildnum
	$(CC) $(CFLAGS) -DBUILD_NUMBER=$(NEXT_BUILDNUM) -c -o $@ $<

.buildnum:
.PHONY: all clean test .buildnum

%.o: %.c sewerpipe.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TARGET)
	@test/run_tests.sh

clean:
	rm -f $(TARGET) $(OBJS)
