#include "telemetry.h"
#include <stdio.h>
#include <math.h>
int parse_flight(const char *path, Observation *observations, size_t capacity) {
    FILE *stream = fopen(path, "r"); char line[256]; size_t count = 0;
    if (!stream) return -1;
    if (!fgets(line,sizeof(line),stream)) { fclose(stream); return -1; }
    while (fgets(line,sizeof(line),stream)) {
        Observation contact; char trailing;
        if (count == capacity || sscanf(line,"%d,%lf,%lf,%lf,%lf %c", &contact.sample,
            &contact.airspeed,&contact.altitude,&contact.hdop,&contact.snr,&trailing) != 5 ||
            !isfinite(contact.airspeed) || !isfinite(contact.altitude) ||
            !isfinite(contact.hdop) || !isfinite(contact.snr) || contact.airspeed < 0 ||
            contact.altitude < 0 || contact.hdop < 0 ||
            (count > 0 && contact.sample <= observations[count-1].sample)) {
            fclose(stream); return -1;
        }
        observations[count++] = contact;
    }
    int failed = ferror(stream); fclose(stream);
    return failed ? -1 : (int)count;
}
