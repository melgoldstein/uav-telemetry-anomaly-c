#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "telemetry.h"

#define FIXTURE "tests/fixtures/edge_cases.csv"

static unsigned int checks;
static unsigned int failures;

#define CHECK(cond)                                                         \
    do {                                                                    \
        checks++;                                                           \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                   \
    } while (0)

#define CHECK_NEAR(a, b, tol) CHECK(fabs((a) - (b)) <= (tol))

static uav_frame_t level_frame(double t, double alt)
{
    uav_frame_t f = { 0.0, 34.9, -116.8, 0.0, 44.0, 0.9, 11u };

    f.t_s = t;
    f.alt_m = alt;
    return f;
}

static void test_window_stats(void)
{
    const double x[] = { 1.0, 2.0, 3.0, 4.0 };
    double mean;
    double sigma;

    window_stats(x, 4, &mean, &sigma);
    CHECK_NEAR(mean, 2.5, 1e-12);
    CHECK_NEAR(sigma, 1.2909944487358056, 1e-12);

    window_stats(x, 1, &mean, &sigma);
    CHECK_NEAR(mean, 1.0, 1e-12);
    CHECK_NEAR(sigma, 0.0, 1e-12);

    window_stats(NULL, 0, &mean, &sigma);
    CHECK(mean == 0.0 && sigma == 0.0);
}

static void test_ew_score(void)
{
    const gnss_baseline_t base = { 44.0, 0.9 };
    uav_frame_t f = level_frame(0.0, 100.0);

    CHECK_NEAR(ew_jamming_score(&f, &base), 1.0, 1e-12);

    /* 6.0206 dB is exactly a 2x amplitude ratio, so HDOP x2 gives J = 4 */
    f.cn0_dbhz = 44.0 - 20.0 * log10(2.0);
    f.hdop = 1.8;
    CHECK_NEAR(ew_jamming_score(&f, &base), 4.0, 1e-9);

    f.cn0_dbhz = 50.0;
    f.hdop = 0.9;
    CHECK_NEAR(ew_jamming_score(&f, &base), 1.0, 1e-12);
}

static void test_missing_gnss_quality(void)
{
    uav_frame_t frames[15];
    anomaly_result_t out[15];
    gnss_baseline_t base;
    size_t i;

    for (i = 0; i < 15; i++) {
        frames[i] = level_frame((double)i, 150.0);
        frames[i].cn0_dbhz = NAN;
    }
    frames[12].hdop = 3.6;
    base = gnss_baseline(frames, 15);
    CHECK(isnan(base.cn0_dbhz));
    CHECK_NEAR(base.hdop, 0.9, 1e-12);
    CHECK_NEAR(ew_jamming_score(&frames[12], &base), 4.0, 1e-9);

    for (i = 0; i < 15; i++)
        frames[i].hdop = NAN;
    base = gnss_baseline(frames, 15);
    CHECK(isnan(ew_jamming_score(&frames[12], &base)));
    CHECK(detect_anomalies(frames, 15, &base, out) == 0);
}

static void test_level_flight_is_clean(void)
{
    uav_frame_t frames[30];
    anomaly_result_t out[30];
    gnss_baseline_t base;
    size_t i;

    for (i = 0; i < 30; i++)
        frames[i] = level_frame((double)i, 150.0 + ((i % 2u) ? 0.3 : -0.3));
    base = gnss_baseline(frames, 30);
    CHECK(detect_anomalies(frames, 30, &base, out) == 0);
}

static void test_altitude_step_flags_kinematic(void)
{
    uav_frame_t frames[20];
    anomaly_result_t out[20];
    gnss_baseline_t base;
    size_t i;

    for (i = 0; i < 20; i++)
        frames[i] = level_frame((double)i, (i >= 12 && i < 15) ? 210.0 : 150.0);
    base = gnss_baseline(frames, 20);
    CHECK(detect_anomalies(frames, 20, &base, out) == 2);
    CHECK(out[12].flags == ANOM_KINEMATIC);
    CHECK(out[15].flags == ANOM_KINEMATIC);
    CHECK(out[13].flags == ANOM_NONE);
}

static void test_z_bound_below_hard_limit(void)
{
    uav_frame_t frames[20];
    anomaly_result_t out[20];
    gnss_baseline_t base;
    size_t i;

    for (i = 0; i < 20; i++)
        frames[i] = level_frame((double)i, (i >= 10) ? 158.0 : 150.0);
    base = gnss_baseline(frames, 20);
    detect_anomalies(frames, 20, &base, out);
    CHECK(fabs(out[10].roc_mps) < 12.0);
    CHECK(out[10].flags == ANOM_KINEMATIC);
}

static void test_jamming_and_timebase(void)
{
    uav_frame_t frames[15];
    anomaly_result_t out[15];
    gnss_baseline_t base;
    size_t i;

    for (i = 0; i < 15; i++)
        frames[i] = level_frame((double)i, 150.0);
    frames[11].cn0_dbhz = 29.0;
    frames[13].t_s = frames[12].t_s;
    base = gnss_baseline(frames, 15);
    detect_anomalies(frames, 15, &base, out);
    CHECK(out[11].flags == ANOM_EW_JAM);
    CHECK(out[13].flags == ANOM_TIMEBASE);
    CHECK(out[14].flags == ANOM_NONE);
}

static void test_parser_rejects_bad_records(void)
{
    uav_frame_t frames[16];
    size_t n = 0;

    CHECK(parse_flight_log(FIXTURE, frames, 16, &n) == 0);
    CHECK(n == 4);
    if (n == 4) {
        CHECK_NEAR(frames[0].t_s, 0.0, 1e-12);
        CHECK_NEAR(frames[1].t_s, 5.0, 1e-12);
        CHECK_NEAR(frames[2].t_s, 7.0, 1e-12);
        CHECK(frames[2].sats == 11u);
        CHECK(isnan(frames[3].cn0_dbhz) && isnan(frames[3].hdop));
    }

    CHECK(parse_flight_log(FIXTURE, frames, 2, &n) == 0);
    CHECK(n == 2);

    CHECK(parse_flight_log("tests/fixtures/does_not_exist.csv", frames, 16, &n) == -1);
    CHECK(n == 0);
}

int main(void)
{
    test_window_stats();
    test_ew_score();
    test_missing_gnss_quality();
    test_level_flight_is_clean();
    test_altitude_step_flags_kinematic();
    test_z_bound_below_hard_limit();
    test_jamming_and_timebase();
    test_parser_rejects_bad_records();

    printf("%u checks, %u failures\n", checks, failures);
    return (failures == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
