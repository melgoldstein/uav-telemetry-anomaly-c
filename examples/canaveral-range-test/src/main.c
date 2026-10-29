#include "telemetry.h"
#include <stdio.h>
int main(int argc, char **argv) {
    Observation observations[512];
    if (argc != 2) { fprintf(stderr,"Usage: telemetry flight.csv\n"); return 1; }
    int count = parse_flight(argv[1],observations,512);
    if (count <= 0) { fprintf(stderr,"Invalid or empty telemetry\n"); return 1; }
    summarize_flight(observations,(size_t)count); return 0;
}
