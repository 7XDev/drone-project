# ─── CrazyDrone Library + Examples ─────────────────────────────────────────
#
# Build all:    make
# Build one:    make examples/wasd_control
# Clean:        make clean
#
# Requires: gcc, Linux/macOS (POSIX sockets + termios)

CC       = gcc
CFLAGS   = -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=199309L -pthread
LDFLAGS  = -lm -pthread

LIB_SRC  = lib/crazydrone.c
LIB_HDR  = lib/crazydrone.h
LIB_OBJ  = lib/crazydrone.o

EXAMPLES = examples/wasd_control \
           examples/controller_control \
           examples/controller_test

.PHONY: all clean help

all: $(EXAMPLES)
	@echo ""
	@echo "✓ All examples built successfully!"
	@echo ""
	@echo "  ./examples/wasd_control        — WASD keyboard flight control"
	@echo "  ./examples/controller_control  — PS4 controller flight control"
	@echo "  ./examples/controller_test     — PS4 controller input logger"
	@echo ""

# ── Library object ──────────────────────────────────────────────────────────

$(LIB_OBJ): $(LIB_SRC) $(LIB_HDR)
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Examples ────────────────────────────────────────────────────────────────

examples/wasd_control: examples/wasd_control.c $(LIB_OBJ) $(LIB_HDR)
	$(CC) $(CFLAGS) -o $@ examples/wasd_control.c $(LIB_OBJ) $(LDFLAGS)

examples/controller_control: examples/controller_control.c $(LIB_OBJ) $(LIB_HDR)
	$(CC) $(CFLAGS) -o $@ examples/controller_control.c $(LIB_OBJ) $(LDFLAGS)

examples/controller_test: examples/controller_test.c
	$(CC) $(CFLAGS) -o $@ examples/controller_test.c $(LDFLAGS)

# ── Housekeeping ────────────────────────────────────────────────────────────

clean:
	rm -f $(LIB_OBJ) $(EXAMPLES)

help:
	@echo "Targets:"
	@echo "  all    — Build library + all examples (default)"
	@echo "  clean  — Remove all build artifacts"
	@echo "  help   — Show this message"
