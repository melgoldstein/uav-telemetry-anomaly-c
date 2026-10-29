#ifndef TELEMETRY_H
#define TELEMETRY_H
#include <stddef.h>
typedef struct { int sample; double airspeed, altitude, hdop, snr; } Observation;
int parse_flight(const char *path, Observation *observations, size_t capacity);
void summarize_flight(const Observation *observations, size_t count);
#endif
