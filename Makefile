CC       = gcc
CFLAGS   = -std=c99 -O2 -Wall -Wextra -Wpedantic -Wshadow
CPPFLAGS = -Iinclude
LDLIBS   = -lm

SRC = $(wildcard src/*.c)
OBJ = $(patsubst src/%.c,obj/%.o,$(SRC))
BIN = bin/telemetry_monitor
LOG = data/flight_log_04.csv

all: $(BIN)

$(BIN): $(OBJ) | bin
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

obj/%.o: src/%.c include/telemetry.h | obj
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

bin obj:
	mkdir -p $@

run: $(BIN)
	./$(BIN) $(LOG)

clean:
	rm -rf bin obj

.PHONY: all run clean
