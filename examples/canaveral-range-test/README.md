# UAV telemetry quality monitor

C99 sliding-window analysis of generated Merritt Island flight telemetry.
Quality flags indicate HDOP/SNR degradation; they do not establish EW attribution.

```text
make
./telemetry data/flight_log_04.csv
sample,mean_airspeed_mps,mean_altitude_m,quality_flag
```

The parser rejects non-finite, malformed, out-of-order and excessive records.
Statistics use the current and up to four previous observations. Thresholds are
illustrative: HDOP above 4 combined with SNR below 20 dB.
Build with `gcc -Wall -Wextra -Werror -std=c99 -Iinclude src/*.c -lm -o telemetry`.
Check with `python tests/check.py ./telemetry`.
