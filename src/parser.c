#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "telemetry.h"

#define LINE_BUF 256

int parse_flight_log(const char *path, uav_frame_t *frames, size_t cap, size_t *count)
{
    char line[LINE_BUF];
    unsigned long lineno = 0;
    FILE *fp = fopen(path, "r");

    *count = 0;
    if (fp == NULL) {
        fprintf(stderr, "parse_flight_log: %s: %s\n", path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof line, fp) != NULL) {
        uav_frame_t f;

        lineno++;
        if (lineno == 1 || line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        if (sscanf(line, "%lf,%lf,%lf,%lf,%lf,%lf,%u",
                   &f.t_s, &f.lat_deg, &f.lon_deg, &f.alt_m,
                   &f.cn0_dbhz, &f.hdop, &f.sats) != 7) {
            fprintf(stderr, "parse_flight_log: %s:%lu: malformed record, dropped\n",
                    path, lineno);
            continue;
        }
        if (*count == cap) {
            fprintf(stderr, "parse_flight_log: buffer full at %lu frames, truncating\n",
                    (unsigned long)cap);
            break;
        }
        frames[(*count)++] = f;
    }

    if (ferror(fp)) {
        fprintf(stderr, "parse_flight_log: %s: read error\n", path);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return (*count > 0) ? 0 : -1;
}
