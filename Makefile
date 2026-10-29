CC       = gcc
CFLAGS   = -std=c99 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
           -Wmissing-prototypes -Wcast-qual -Wwrite-strings $(EXTRA_CFLAGS)
CPPFLAGS = -Iinclude
LDLIBS   = -lm

LIB_SRC  = src/parser.c src/anomaly.c
LIB_OBJ  = $(patsubst src/%.c,obj/%.o,$(LIB_SRC))
BIN      = bin/telemetry_monitor
TEST_BIN = bin/test_telemetry
LOG      = data/flight_log_04.csv

all: $(BIN)

$(BIN): $(LIB_OBJ) obj/main.o | bin
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_BIN): $(LIB_OBJ) obj/test_telemetry.o | bin
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

obj/%.o: src/%.c include/telemetry.h | obj
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

obj/%.o: tests/%.c include/telemetry.h | obj
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

bin obj:
	mkdir -p $@

run: $(BIN)
	./$(BIN) $(LOG)

test: $(TEST_BIN)
	./$(TEST_BIN)

# ASan/UBSan build; requires a toolchain with sanitizer runtimes (Linux gcc/clang)
sanitize: clean
	$(MAKE) test run EXTRA_CFLAGS="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"

clean:
	rm -rf bin obj

.PHONY: all run test sanitize clean
