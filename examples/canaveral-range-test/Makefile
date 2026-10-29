CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Werror -O2 -Iinclude
telemetry: src/main.c src/parser.c src/anomaly.c include/telemetry.h
	$(CC) $(CFLAGS) src/main.c src/parser.c src/anomaly.c -lm -o telemetry
clean:
	rm -f telemetry telemetry.exe
