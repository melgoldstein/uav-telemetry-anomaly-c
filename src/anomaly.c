#include <math.h>

#include "telemetry.h"

/* First 10 s of the log are pre-mission open-sky reference, before any EW exposure */
#define BASELINE_FRAMES   10u
#define ROC_WINDOW        8u
/* Sustained climb/sink beyond 12 m/s is outside the Group 2 fixed-wing envelope */
#define ROC_LIMIT_MPS     12.0
/* 1 Hz GNSS altitude noise floor; prevents z blow-up during level flight */
#define ROC_SIGMA_FLOOR   1.5
#define ROC_Z_LIMIT       4.0
/* J = 3.0: predicted horizontal error has tripled relative to baseline */
#define EW_SCORE_LIMIT    3.0
/* L1 C/A carrier tracking degrades below ~30 dB-Hz on commercial receivers */
#define CN0_FLOOR_DBHZ    30.0

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

double ew_jamming_score(const uav_frame_t *f, const gnss_baseline_t *base)
{
    double cn0_loss_db = base->cn0_dbhz - f->cn0_dbhz;

    if (cn0_loss_db < 0.0)
        cn0_loss_db = 0.0;
    /*
     * Pseudorange noise scales with (C/N0)^-1/2, i.e. a 20*log10 amplitude ratio,
     * and horizontal error scales with HDOP. J is the product: predicted growth
     * of horizontal position error over the open-sky baseline.
     */
    return (f->hdop / base->hdop) * pow(10.0, cn0_loss_db / 20.0);
}

size_t detect_anomalies(const uav_frame_t *frames, size_t n,
                        const gnss_baseline_t *base, anomaly_result_t *out)
{
    double roc_hist[ROC_WINDOW];
    size_t hist_len = 0;
    size_t head = 0;
    size_t flagged = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        const uav_frame_t *f = &frames[i];
        anomaly_result_t *r = &out[i];

        r->flags = ANOM_NONE;
        r->roc_mps = 0.0;
        r->roc_z = 0.0;
        r->ew_jamming_score = ew_jamming_score(f, base);

        if (i > 0) {
            const double dt = f->t_s - frames[i - 1].t_s;

            if (dt <= 0.0) {
                r->flags |= ANOM_TIMEBASE;
            } else {
                double mean;
                double sigma;

                r->roc_mps = (f->alt_m - frames[i - 1].alt_m) / dt;
                window_stats(roc_hist, hist_len, &mean, &sigma);
                if (hist_len >= 3)
                    r->roc_z = (r->roc_mps - mean) / fmax(sigma, ROC_SIGMA_FLOOR);

                if (fabs(r->roc_mps) > ROC_LIMIT_MPS || fabs(r->roc_z) > ROC_Z_LIMIT) {
                    r->flags |= ANOM_KINEMATIC;
                } else {
                    /* rejected samples stay out of the window so a spoofed step cannot widen sigma */
                    roc_hist[head] = r->roc_mps;
                    head = (head + 1) % ROC_WINDOW;
                    if (hist_len < ROC_WINDOW)
                        hist_len++;
                }
            }
        }

        if (r->ew_jamming_score > EW_SCORE_LIMIT || f->cn0_dbhz < CN0_FLOOR_DBHZ)
            r->flags |= ANOM_EW_JAM;
        if (r->flags != ANOM_NONE)
            flagged++;
    }
    return flagged;
}
