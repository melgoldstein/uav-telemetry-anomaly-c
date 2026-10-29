#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "telemetry.h"

#define LINE_BUF 256
/* GNSS C/N0 above 60 dB-Hz is not physically achievable at L1; treat as corrupt */
#define CN0_MAX_DBHZ 60.0

static int frame_in_range(const uav_frame_t *f)
{
    return f->lat_deg >= -90.0 && f->lat_deg <= 90.0 &&
           f->lon_deg >= -180.0 && f->lon_deg <= 180.0 &&
           (isnan(f->cn0_dbhz) || (f->cn0_dbhz >= 0.0 && f->cn0_dbhz <= CN0_MAX_DBHZ)) &&
           (isnan(f->hdop) || f->hdop > 0.0);
}

static int only_space(const char *s)
{
    while (*s != '\0') {
        if (!isspace((unsigned char)*s))
            return 0;
        s++;
    }
    return 1;
}

static void discard_rest_of_line(FILE *fp)
{
    int c;

    while ((c = fgetc(fp)) != EOF && c != '\n')
        ;
}

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
        int consumed = 0;

        lineno++;
        if (strchr(line, '\n') == NULL && !feof(fp)) {
            fprintf(stderr, "parse_flight_log: %s:%lu: record exceeds %d bytes, dropped\n",
                    path, lineno, LINE_BUF - 1);
            discard_rest_of_line(fp);
            continue;
        }
        if (lineno == 1 || line[0] == '#' || only_space(line))
            continue;

        if (sscanf(line, "%lf,%lf,%lf,%lf,%lf,%lf,%u%n",
                   &f.t_s, &f.lat_deg, &f.lon_deg, &f.alt_m,
                   &f.cn0_dbhz, &f.hdop, &f.sats, &consumed) != 7 ||
            !only_space(line + consumed)) {
            fprintf(stderr, "parse_flight_log: %s:%lu: malformed record, dropped\n",
                    path, lineno);
            continue;
        }
        if (!frame_in_range(&f)) {
            fprintf(stderr, "parse_flight_log: %s:%lu: field out of range, dropped\n",
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
