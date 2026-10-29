#!/bin/sh
# Download the logs listed in data/px4/SOURCES and regenerate the 1 Hz CSVs.
# Raw .ulg files (~300 MB total) are cached in ${ULOG_CACHE:-.ulog_cache}.
set -eu

cache=${ULOG_CACHE:-.ulog_cache}
mkdir -p "$cache" data/px4
"${MAKE:-make}" bin/ulog2csv

grep -v '^#' data/px4/SOURCES | while read -r id _; do
    short=${id%%-*}
    if [ ! -f "$cache/$id.ulg" ]; then
        curl -sSfL -o "$cache/$id.ulg" "https://review.px4.io/download?log=$id"
    fi
    ./bin/ulog2csv "$cache/$id.ulg" "data/px4/$short.truth.csv" > "data/px4/$short.csv"
done
