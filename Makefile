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

bin/ulog2csv: tools/ulog2csv.c | bin
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

bin obj:
	mkdir -p $@

run: $(BIN)
	./$(BIN) $(LOG)

test: $(TEST_BIN)
	./$(TEST_BIN)

tools: bin/ulog2csv

# Regression check against the committed real-flight results
validate: $(BIN)
	sh scripts/px4_summary.sh > obj/px4_summary.txt
	diff -u data/px4/expected_summary.txt obj/px4_summary.txt

# ASan/UBSan build; requires a toolchain with sanitizer runtimes (Linux gcc/clang)
sanitize: clean
	$(MAKE) test run validate EXTRA_CFLAGS="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"

clean:
	rm -rf bin obj

.PHONY: all run test tools validate sanitize clean
