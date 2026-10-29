# uav-telemetry-anomaly-c

Offline anomaly screen for UAV GNSS/air-data telemetry logs. C99, no dependencies beyond libc and libm.

## Overview

`telemetry_monitor` ingests a 1 Hz CSV flight log and flags three classes of fault per frame:

| Flag | Meaning |
|------|---------|
| `KIN` | Kinematically implausible altitude step (rate-of-climb bound or z-score violation). Typical signature of GNSS spoofing or corrupted position solution. |
| `EWJ` | Electronic-warfare jamming signature: correlated C/N0 loss and HDOP growth. |
| `TBE` | Timebase error: non-monotonic or duplicate timestamps. |

Input schema (header row required):

```
t_s,lat_deg,lon_deg,alt_m,cn0_dbhz,hdop,sats
```

## Build & Execution

```
make
./bin/telemetry_monitor data/flight_log_04.csv
```

The log path defaults to `data/flight_log_04.csv` when omitted. `make run` builds and runs against the default log. On Windows with MinGW-w64, use `mingw32-make`.

Exit status: `0` on successful analysis, `1` on I/O or parse failure. Diagnostics go to `stderr`.

## Mathematical Formulation

### Kinematic rate-of-climb bounds

Rate of climb from consecutive frames:

```
ROC_i = (h_i - h_(i-1)) / (t_i - t_(i-1))
```

A frame is flagged `KIN` if either bound is violated:

```
|ROC_i| > ROC_max                          ROC_max = 12.0 m/s
|ROC_i - mu_w| / max(sigma_w, sigma_min) > 4.0   sigma_min = 1.5 m/s
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

## Directory Structure

```
.
|-- Makefile
|-- README.md
|-- data/
|   `-- flight_log_04.csv     60 s fixed-wing segment: climb, cruise, jamming, altitude spoof
|-- include/
|   `-- telemetry.h           frame/result types, flag bits, public API
`-- src/
    |-- anomaly.c             window statistics, GNSS baseline, J score, detection
    |-- main.c                CLI and report formatting
    `-- parser.c              CSV ingest with per-record validation
```

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
