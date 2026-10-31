# uav-telemetry-anomaly-c

[Portfolio: melgoldstein.com](https://melgoldstein.com/)

[![ci](https://github.com/melgoldstein/uav-telemetry-anomaly-c/actions/workflows/ci.yml/badge.svg)](https://github.com/melgoldstein/uav-telemetry-anomaly-c/actions/workflows/ci.yml)
![C99](https://img.shields.io/badge/std-C99-blue)
[![License: MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)

Offline anomaly screen for UAV GNSS telemetry logs. C99, no dependencies beyond libc and libm, no dynamic allocation.

## Overview

`telemetry_monitor` ingests a 1 Hz CSV flight log and flags three classes of fault per frame:

| Flag | Meaning |
|------|---------|
| `KIN` | Kinematically implausible altitude step (rate-of-climb bound or z-score violation). Typical signature of GNSS spoofing or corrupted position solution. |
| `EWJ` | Electronic-warfare jamming signature: correlated C/N0 loss and HDOP growth. |
| `TBE` | Timebase error: non-monotonic or duplicate timestamps. |

Input schema (header row required, `#` lines ignored):

```
t_s,lat_deg,lon_deg,alt_m,cn0_dbhz,hdop,sats
```

Records are rejected, with a `file:line` diagnostic on `stderr`, if they have the wrong field count, trailing fields, exceed 255 bytes, or carry out-of-range values (`|lat| > 90`, `|lon| > 180`, `C/N0` outside 0-60 dB-Hz, `HDOP <= 0`). A single bad record never aborts the run. `nan` in the `cn0_dbhz` or `hdop` column means the receiver did not report that value; see [Missing GNSS quality data](#missing-gnss-quality-data).

## Build & Execution

Requires a C99 compiler and GNU Make. Tested with GCC and Clang on Ubuntu (CI) and GCC 16 / MinGW-w64 on Windows (use `mingw32-make`).

```
make
./bin/telemetry_monitor data/flight_log_04.csv
```

The log path defaults to `data/flight_log_04.csv` when omitted.

| Target | Action |
|--------|--------|
| `make` | Build `bin/telemetry_monitor` |
| `make run` | Build and run against the reference log |
| `make test` | Build and run unit tests |
| `make tools` | Build `bin/ulog2csv` (PX4 ULog converter) |
| `make validate` | Re-run the real-flight study and diff against `data/px4/expected_summary.txt` |
| `make sanitize` | Rebuild under ASan + UBSan, run tests, reference log and real-flight study |
| `make clean` | Remove `bin/` and `obj/` |

`EXTRA_CFLAGS` appends compiler flags, e.g. `make EXTRA_CFLAGS=-Werror`.

Exit status: `0` on successful analysis, `1` on I/O failure or no usable frames.

## Verification

`tests/test_telemetry.c` covers:

- `window_stats` against hand-computed mean and unbiased sigma, including `n = 0` and `n = 1`
- `J` identity at baseline and the closed-form case `dC/N0 = 20 log10 2`, `HDOP x2` giving `J = 4`
- Zero false positives on noisy level flight
- Altitude step in and out (hard bound), and an 8 m/s step caught by the z-score bound alone
- C/N0 floor and duplicate-timestamp detection
- Missing C/N0 (J falls back to the HDOP ratio) and missing HDOP (J undefined, no EWJ flag)
- Parser rejection of short, long, oversize and out-of-range records, acceptance of `nan`, buffer truncation, and missing files

The parser tests print the expected rejection diagnostics to `stderr`. CI runs the full suite under GCC and Clang with `-Werror`, runs it again under AddressSanitizer and UndefinedBehaviorSanitizer, re-runs the real-flight study as a regression check, and runs `cppcheck` over `src/`, `tests/` and `tools/`.

## Mathematical Formulation

### Kinematic rate-of-climb bounds

Rate of climb from consecutive frames:

```
ROC_i = (h_i - h_(i-1)) / (t_i - t_(i-1))
```

A frame is flagged `KIN` if either bound is violated:

```
|ROC_i| > ROC_max                                  ROC_max   = 12.0 m/s
|ROC_i - mu_w| / max(sigma_w, sigma_min) > 4.0     sigma_min = 1.5 m/s
```

`mu_w` and `sigma_w` are the sample mean and standard deviation over a sliding window of the last 8 accepted ROC samples. Rejected samples are excluded from the window so a spoofed step cannot inflate `sigma_w` and mask the return transition. `sigma_min` is the 1 Hz GNSS vertical noise floor; it prevents the z-score from diverging during level flight.

### SNR-to-HDOP jamming ratio

Baseline `C/N0_0` and `HDOP_0` are the means of the first 10 frames (pre-mission open sky). For each frame:

```
dC/N0 = max(0, C/N0_0 - C/N0_i)            [dB]
J_i   = (HDOP_i / HDOP_0) * 10^(dC/N0 / 20)
```

Code-tracking pseudorange noise scales as `(C/N0)^(-1/2)`, which is the 20 log10 amplitude ratio above. Horizontal position error scales as `HDOP * sigma_UERE`. `J` is therefore the predicted growth factor of horizontal position error relative to baseline. A frame is flagged `EWJ` when:

```
J_i > 3.0   or   C/N0_i < 30 dB-Hz
```

30 dB-Hz is the approximate L1 C/A carrier-tracking threshold for commercial receivers.

### Missing GNSS quality data

Real logs do not always carry both inputs to `J`, so the detector degrades rather than guessing:

| Available | Behaviour |
|-----------|-----------|
| C/N0 and HDOP | Full `J` and C/N0 floor |
| HDOP only | `dC/N0 = 0`, so `J = HDOP_i / HDOP_0`; C/N0 floor disabled |
| Neither | `J` undefined (`n/a`); `EWJ` never raised; `KIN` and `TBE` unaffected |

## Directory Structure

```
.
|-- .github/workflows/ci.yml  GCC/Clang matrix, sanitizers, real-flight regression, cppcheck
|-- Makefile
|-- data/
|   |-- flight_log_04.csv     synthetic 60 s segment: climb, cruise, jamming, altitude spoof
|   `-- px4/                  5 real PX4 flights at 1 Hz, baro truth files, expected summary
|-- include/
|   `-- telemetry.h           frame/result types, flag bits, public API contracts
|-- scripts/
|   |-- px4_fetch.sh          download logs from PX4 Flight Review and convert
|   `-- px4_summary.sh        run detector on each flight, cross-check KIN against baro
|-- src/
|   |-- anomaly.c             window statistics, GNSS baseline, J score, detection
|   |-- main.c                CLI and report formatting
|   `-- parser.c              CSV ingest with per-record validation
|-- tests/
|   |-- fixtures/edge_cases.csv
|   `-- test_telemetry.c
`-- tools/
    `-- ulog2csv.c            PX4 ULog binary parser, emits the CSV schema above
```

## Design Notes

- Fixed-capacity static buffers (`MAX_FRAMES = 4096`, about 68 min at 1 Hz) and no heap use, per rule 3 of the JPL "Power of Ten" guidelines for safety-critical C.
- The detector is a single O(n) pass. The ROC window is a fixed 8-slot ring buffer, so per-frame cost is constant.
- Detection is stateless across runs and deterministic: the same log always produces the same report.
- Library code (`parser.c`, `anomaly.c`) is kept apart from I/O formatting (`main.c`), so the tests link against the library objects directly.
- `tools/ulog2csv.c` is a host-side tool and does use the heap, because the number and length of format definitions in a ULog file are not known in advance. It resolves message layouts from the definitions in each file, including nested types, so it reads both the pre-v1.15 (`int32` 1e-7 deg) and v1.15+ (`double` deg) GPS message layouts without hard-coded offsets.

## Sample Output

`./bin/telemetry_monitor data/flight_log_04.csv`

```
UAV TELEMETRY ANOMALY MONITOR
source    : data/flight_log_04.csv
frames    : 60  (t = 0.0 .. 59.0 s)
baseline  : C/N0 44.1 dB-Hz, HDOP 0.90
------------------------------------------------------------------------
   t[s]   alt[m]  roc[m/s]   roc_z   C/N0   HDOP  SV       J  FLAGS
------------------------------------------------------------------------
   21.0    149.4      0.20    0.04   39.8   1.89  10    3.45  EWJ
   22.0    149.2     -0.20   -0.15   35.7   3.01   9    8.82  EWJ
   23.0    149.2      0.00   -0.02   32.0   4.07   7   18.25  EWJ
   24.0    149.5      0.30    0.18   27.5   4.95   6   37.26  EWJ
   25.0    149.4     -0.10   -0.12   26.6   5.36   6   44.76  EWJ
   26.0    149.5      0.10    0.03   28.7   5.02   6   32.92  EWJ
   27.0    149.4     -0.10   -0.13   30.2   4.53   7   24.99  EWJ
   28.0    149.1     -0.30   -0.23   31.1   3.86   7   19.20  EWJ
   29.0    149.4      0.30    0.21   33.0   3.47   8   13.87  EWJ
   38.0    213.9     63.80   42.48   43.6   0.92  11    1.09  KIN
   41.0    150.0    -64.80  -43.35   44.0   0.85  11    0.96  KIN
------------------------------------------------------------------------
flagged   : 11 / 60 frames  (KIN 2, EWJ 9)
```

Frames 21-29: broadband L1 jamming onset and release; C/N0 drops 17 dB, HDOP rises ~6x, tracked SVs fall 11 to 6. Frames 38 and 41: a 64 m altitude step in and out of a spoofed solution, with no corresponding EW signature.

## Validation on Real Flight Data

The synthetic log shows that each detector path fires. It cannot show how often the detector fires when nothing is wrong, so the detector was also run on real flights.

**Dataset.** Five fixed-wing mission flights from the public [PX4 Flight Review](https://review.px4.io/browse) archive (license CC-BY PX4), totalling 5,598 s (93 min) after the first 3D fix. They cover four flight controllers and PX4 v1.14 to v1.18; IDs are listed in [`data/px4/SOURCES`](data/px4/SOURCES). `tools/ulog2csv` converts each binary log to the 1 Hz CSV schema and writes a separate truth file holding barometric altitude and the receiver's own `jamming_state` / `spoofing_state` for the same second. The truth file is never shown to the detector.

**Reproduce.**

```
make tools
sh scripts/px4_fetch.sh       # downloads ~300 MB of .ulg into .ulog_cache/
sh scripts/px4_summary.sh
```

**Result** (`data/px4/expected_summary.txt`, checked in CI by `make validate`):

```
flight     frames   hdop   KIN   EWJ baro_confirms rx_spoof_s
0ed09c07     2100   0.49     2     0          2/2         32
1c87e004      779    n/a     0     0          0/0          0
36ecfadf      860   0.49     0     0          0/0          1
ec71ea78     1350   0.56    11     0        10/11          0
fa8a4cc9      509    n/a     1     0          1/1          0
```

`baro_confirms` counts KIN flags where barometric rate of climb over the same second has the same sign and at least half the GNSS magnitude, meaning the aircraft really moved. `rx_spoof_s` is the number of seconds the receiver itself declared spoofing.

**Findings.**

1. **Kinematic bound: 14 flags in 5,598 frames (0.25 %), all false alarms.** 13 are confirmed by the barometer as real vertical manoeuvres of 6 to 14 m/s. The remaining one (`ec71ea78`, t = 469 s) has the barometer moving the same way at 47 % of the GNSS rate, just under the 50 % rule. None of the flights contain a known spoofing event, so these logs measure the false-alarm rate only, not the detection rate. All 14 flags come from aggressive but legitimate climbs and dives, which a single-sensor z-score cannot tell apart from a position jump. Cross-checking against barometric altitude is the natural fix.
2. **Receiver spoofing alerts had no measurable effect.** The receiver on `0ed09c07` declared spoofing for two 16 s windows (t = 806-821 s and 869-884 s). Over both windows, ground speed stayed at 16-28 m/s and the GNSS-minus-baro altitude offset stayed within its normal 64-70 m band. The detector raised nothing there, which is consistent with no position displacement.
3. **Jamming score: 0 flags.** HDOP stayed between 0.45 and 0.60 on the three flights that report it, with 31-32 satellites in use. The two CubeOrange+ v1.16.0 flights report `jamming_state = WARNING` for 3,390 of 3,450 s, yet HDOP and satellite count show no degradation. Without C/N0 this disagreement cannot be resolved either way.
4. **Real logs lack C/N0, and some lack HDOP.** None of the five logs record `satellite_info`, the topic that carries per-satellite C/N0. Two receivers report HDOP as a constant 0. This motivated the missing-data handling above: in such logs `J` reduces to the HDOP ratio, or `EWJ` is disabled.

## Limitations

- `data/flight_log_04.csv` is a synthetic test vector. It was generated to exercise each detector path and was not recorded from an aircraft. The real-flight study measures false alarms only, because none of the five flights contains a confirmed spoofing or jamming event.
- The `KIN` z-score bound fires on legitimate manoeuvres of about 6 m/s and above (finding 1). Fusing barometric altitude, for example with a Kalman filter and an innovation test, would reduce this.
- The baseline assumes the first 10 frames are free of interference. A log that starts inside a jammed area will under-report `J`.
- Altitude is the only kinematic channel checked. Horizontal spoofing that keeps a plausible vertical profile, or a slow altitude drift under 4 sigma per frame, is not detected. Cross-checking against barometric altitude or IMU dead reckoning would close this gap.
- Thresholds are fixed at compile time and sized for a small fixed-wing airframe at 1 Hz. Other platforms or sample rates need re-tuning.

## References

1. E. D. Kaplan and C. J. Hegarty, *Understanding GPS/GNSS: Principles and Applications*, 3rd ed. Artech House, 2017. (C/N0, tracking thresholds, DOP)
2. P. Misra and P. Enge, *Global Positioning System: Signals, Measurements, and Performance*, 2nd ed. Ganga-Jamuna Press, 2006. (pseudorange error budget, `HDOP * sigma_UERE`)
3. PX4 Autopilot, *ULog File Format*, https://docs.px4.io/main/en/dev_log/ulog_file_format.html
4. PX4 Flight Review public log archive, https://review.px4.io/browse (flight data used under CC-BY PX4)

## License

MIT. See [LICENSE](LICENSE).
