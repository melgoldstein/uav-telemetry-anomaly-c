#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stddef.h>

#define MAX_FRAMES 4096

typedef struct {
    double t_s;
    double lat_deg;
    double lon_deg;
    double alt_m;
    double cn0_dbhz;
    double hdop;
    unsigned int sats;
} uav_frame_t;

typedef struct {
    double cn0_dbhz;
    double hdop;
} gnss_baseline_t;

enum {
    ANOM_NONE      = 0u,
    ANOM_KINEMATIC = 1u << 0,
    ANOM_EW_JAM    = 1u << 1,
    ANOM_TIMEBASE  = 1u << 2
};

typedef struct {
    double roc_mps;
    double roc_z;
    double ew_jamming_score;
    unsigned int flags;
} anomaly_result_t;

int parse_flight_log(const char *path, uav_frame_t *frames, size_t cap, size_t *count);

void window_stats(const double *x, size_t n, double *mean, double *sigma);
gnss_baseline_t gnss_baseline(const uav_frame_t *frames, size_t n);
double ew_jamming_score(const uav_frame_t *f, const gnss_baseline_t *base);
size_t detect_anomalies(const uav_frame_t *frames, size_t n,
                        const gnss_baseline_t *base, anomaly_result_t *out);

#endif /* TELEMETRY_H */
