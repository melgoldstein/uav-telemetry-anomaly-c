#!/bin/sh
# Run telemetry_monitor over every converted PX4 flight and cross-check each
# KIN flag against barometric rate of climb from the same log.
set -eu

# A KIN flag is attributed to real vertical motion when baro ROC has the same
# sign and at least half the GNSS magnitude; baro lag and noise at 1 Hz make a
# tighter match unreliable.
printf '%-9s %7s %6s %5s %5s %12s %10s\n' flight frames hdop KIN EWJ baro_confirms rx_spoof_s
for csv in data/px4/*[0-9a-f].csv; do
    id=$(basename "$csv" .csv)
    truth=data/px4/$id.truth.csv
    report=$(./bin/telemetry_monitor "$csv")
    frames=$(printf '%s\n' "$report" | awk '/^frames/{print $3}')
    hdop=$(printf '%s\n' "$report" | awk '/^baseline/{print $NF}')
    kin=$(printf '%s\n' "$report" | awk '/^flagged/{gsub(/[(),]/,""); print $8}')
    ewj=$(printf '%s\n' "$report" | awk '/^flagged/{gsub(/[(),]/,""); print $10}')
    flags=$(printf '%s\n' "$report" | awk '$NF ~ /KIN/ {printf "%d ", $1}')
    confirmed=$(awk -F, -v flags="$flags" '
        FNR == 1 { next }
        FILENAME ~ /truth/ { b[$1 + 0] = $2; if ($4 >= 2) spoof++; next }
        { g[$1 + 0] = $4 }
        END {
            n = split(flags, f, " "); c = 0
            for (i = 1; i <= n; i++) {
                t = f[i]; gr = g[t] - g[t - 1]; br = b[t] - b[t - 1]
                if (gr * br > 0 && (br < 0 ? -br : br) >= 0.5 * (gr < 0 ? -gr : gr)) c++
            }
            printf "%d/%d %d", c, n, spoof + 0
        }' "$truth" "$csv")
    printf '%-9s %7s %6s %5s %5s %12s %10s\n' "$id" "$frames" "$hdop" "$kin" "$ewj" \
        "${confirmed% *}" "${confirmed#* }"
done
