#include <stdio.h>
#include <stdlib.h>

#include "telemetry.h"

#define DEFAULT_LOG "data/flight_log_04.csv"
#define RULE "------------------------------------------------------------------------"

static uav_frame_t frames[MAX_FRAMES];
static anomaly_result_t results[MAX_FRAMES];

static const char *flag_str(unsigned int flags)
{
    static char buf[16];
    int len;

    len = snprintf(buf, sizeof buf, "%s%s%s",
                   (flags & ANOM_KINEMATIC) ? "KIN " : "",
                   (flags & ANOM_EW_JAM) ? "EWJ " : "",
                   (flags & ANOM_TIMEBASE) ? "TBE " : "");
    if (len > 0)
        buf[len - 1] = '\0';
    return buf;
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : DEFAULT_LOG;
    size_t n = 0;
    size_t flagged;
    size_t n_kin = 0;
    size_t n_ewj = 0;
    size_t i;
    gnss_baseline_t base;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [flight_log.csv]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (parse_flight_log(path, frames, MAX_FRAMES, &n) != 0) {
        fprintf(stderr, "telemetry_monitor: no usable frames in %s\n", path);
        return EXIT_FAILURE;
    }

    base = gnss_baseline(frames, n);
    flagged = detect_anomalies(frames, n, &base, results);

    printf("UAV TELEMETRY ANOMALY MONITOR\n");
    printf("source    : %s\n", path);
    printf("frames    : %lu  (t = %.1f .. %.1f s)\n",
           (unsigned long)n, frames[0].t_s, frames[n - 1].t_s);
    printf("baseline  : C/N0 %.1f dB-Hz, HDOP %.2f\n", base.cn0_dbhz, base.hdop);
    printf("%s\n", RULE);
    printf("%7s %8s %9s %7s %6s %6s %3s %7s  %s\n",
           "t[s]", "alt[m]", "roc[m/s]", "roc_z", "C/N0", "HDOP", "SV", "J", "FLAGS");
    printf("%s\n", RULE);

    for (i = 0; i < n; i++) {
        const anomaly_result_t *r = &results[i];

        if (r->flags == ANOM_NONE)
            continue;
        if (r->flags & ANOM_KINEMATIC)
            n_kin++;
        if (r->flags & ANOM_EW_JAM)
            n_ewj++;
        printf("%7.1f %8.1f %9.2f %7.2f %6.1f %6.2f %3u %7.2f  %s\n",
               frames[i].t_s, frames[i].alt_m, r->roc_mps, r->roc_z,
               frames[i].cn0_dbhz, frames[i].hdop, frames[i].sats,
               r->ew_jamming_score, flag_str(r->flags));
    }

    printf("%s\n", RULE);
    printf("flagged   : %lu / %lu frames  (KIN %lu, EWJ %lu)\n",
           (unsigned long)flagged, (unsigned long)n,
           (unsigned long)n_kin, (unsigned long)n_ewj);
    return EXIT_SUCCESS;
}
