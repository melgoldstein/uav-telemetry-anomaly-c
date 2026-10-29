#include <math.h>

#include "telemetry.h"

/* First 10 s of the log are pre-mission open-sky reference, before any EW exposure */
#define BASELINE_FRAMES   10u

void window_stats(const double *x, size_t n, double *mean, double *sigma)
{
    double sum = 0.0;
    double sq = 0.0;
    size_t i;

    *mean = 0.0;
    *sigma = 0.0;
    if (n == 0)
        return;

    for (i = 0; i < n; i++)
        sum += x[i];
    *mean = sum / (double)n;

    for (i = 0; i < n; i++) {
        const double d = x[i] - *mean;
        sq += d * d;
    }
    if (n > 1)
        *sigma = sqrt(sq / (double)(n - 1));
}

gnss_baseline_t gnss_baseline(const uav_frame_t *frames, size_t n)
{
    gnss_baseline_t b = { 0.0, 0.0 };
    const size_t nb = (n < BASELINE_FRAMES) ? n : BASELINE_FRAMES;
    size_t i;

    for (i = 0; i < nb; i++) {
        b.cn0_dbhz += frames[i].cn0_dbhz;
        b.hdop += frames[i].hdop;
    }
    if (nb > 0) {
        b.cn0_dbhz /= (double)nb;
        b.hdop /= (double)nb;
    }
    return b;
}
