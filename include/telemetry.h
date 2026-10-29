#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stddef.h>

#define MAX_FRAMES 4096

typedef struct {
    double t_s;          /* mission elapsed time, s */
    double lat_deg;      /* WGS-84 */
    double lon_deg;      /* WGS-84 */
    double alt_m;        /* GNSS altitude, m MSL */
    double cn0_dbhz;     /* mean carrier-to-noise density of tracked SVs */
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
    unsigned int flags;  /* bitwise OR of ANOM_* */
} anomaly_result_t;

/*
 * Reads up to cap frames from a CSV log. Malformed, oversize and out-of-range
 * records are reported on stderr and skipped. Returns 0 if at least one frame
 * was read, -1 otherwise.
 */
int parse_flight_log(const char *path, uav_frame_t *frames, size_t cap, size_t *count);

/* Sample mean and unbiased (n - 1) standard deviation. Both are 0 for n == 0. */
void window_stats(const double *x, size_t n, double *mean, double *sigma);

/* Open-sky reference taken from the leading frames of the log. */
gnss_baseline_t gnss_baseline(const uav_frame_t *frames, size_t n);

/* Predicted horizontal-error growth factor relative to base; 1.0 at baseline. */
double ew_jamming_score(const uav_frame_t *f, const gnss_baseline_t *base);

/* Fills out[0..n-1] and returns the number of frames with any flag set. */
size_t detect_anomalies(const uav_frame_t *frames, size_t n,
                        const gnss_baseline_t *base, anomaly_result_t *out);

#endif /* TELEMETRY_H */
