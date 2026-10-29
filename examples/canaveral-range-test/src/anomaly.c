#include "telemetry.h"
#include <stdio.h>
void summarize_flight(const Observation *observations, size_t count) {
    puts("sample,mean_airspeed_mps,mean_altitude_m,quality_flag");
    for (size_t index=0; index<count; ++index) {
        size_t start = index > 4 ? index-4 : 0;
        double speed_sum=0, altitude_sum=0;
        for (size_t cursor=start; cursor<=index; ++cursor) {
            speed_sum += observations[cursor].airspeed;
            altitude_sum += observations[cursor].altitude;
        }
        printf("%d,%.3f,%.3f,%d\n",observations[index].sample,
            speed_sum/(index-start+1),altitude_sum/(index-start+1),
            observations[index].hdop > 4 && observations[index].snr < 20);
    }
}
