CC ?= cc
CFLAGS ?= -O0 -std=c99 -Wall -Wextra -Wpedantic

.PHONY: all test clean

all: bf16

bf16: bf16.c
	$(CC) $(CFLAGS) $< -o $@

test: bf16
	./bf16

clean:
	$(RM) bf16
