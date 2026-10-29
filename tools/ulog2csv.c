/*
 * ulog2csv: extract 1 Hz GNSS frames from a PX4 ULog file into the
 * telemetry_monitor CSV schema.
 *
 * Host-side tool. Unlike the detector it uses the heap, since format
 * definitions in a ULog file are variable in number and length.
 *
 * Format reference: https://docs.px4.io/main/en/dev_log/ulog_file_format.html
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FORMATS   1024
#define MAX_MSG_IDS   65536
#define MAX_SATS      64
#define NO_FIELD      (-1)
/* fix_type >= 3 is a 3D fix; earlier frames carry placeholder lat/lon/HDOP */
#define MIN_FIX_TYPE  3

typedef struct {
    char *name;
    char *fields;   /* "type name;type name;..." */
    int size;       /* -1 until resolved */
} ulog_format_t;

typedef struct {
    int offset;
    char type[64];
    int count;
} field_ref_t;

static ulog_format_t formats[MAX_FORMATS];
static size_t n_formats;
static int16_t sub_format[MAX_MSG_IDS];
static uint8_t sub_multi[MAX_MSG_IDS];

static uint64_t rd_u64(const uint8_t *p)
{
    uint64_t v = 0;
    int i;

    for (i = 7; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static double rd_scalar(const uint8_t *p, const char *type)
{
    if (strcmp(type, "double") == 0) {
        uint64_t u = rd_u64(p);
        double d;
        memcpy(&d, &u, sizeof d);
        return d;
    }
    if (strcmp(type, "float") == 0) {
        uint32_t u = rd_u32(p);
        float f;
        memcpy(&f, &u, sizeof f);
        return (double)f;
    }
    if (strcmp(type, "int32_t") == 0)
        return (double)(int32_t)rd_u32(p);
    if (strcmp(type, "uint32_t") == 0)
        return (double)rd_u32(p);
    if (strcmp(type, "uint64_t") == 0)
        return (double)rd_u64(p);
    if (strcmp(type, "int8_t") == 0)
        return (double)(int8_t)p[0];
    return (double)p[0];
}

static int find_format(const char *name)
{
    size_t i;

    for (i = 0; i < n_formats; i++)
        if (strcmp(formats[i].name, name) == 0)
            return (int)i;
    return -1;
}

static int type_size(const char *type)
{
    static const struct { const char *name; int size; } builtin[] = {
        { "int8_t", 1 }, { "uint8_t", 1 }, { "bool", 1 }, { "char", 1 },
        { "int16_t", 2 }, { "uint16_t", 2 }, { "int32_t", 4 }, { "uint32_t", 4 },
        { "float", 4 }, { "int64_t", 8 }, { "uint64_t", 8 }, { "double", 8 }
    };
    size_t i;
    int f;

    for (i = 0; i < sizeof builtin / sizeof builtin[0]; i++)
        if (strcmp(type, builtin[i].name) == 0)
            return builtin[i].size;
    f = find_format(type);
    if (f < 0)
        return -1;
    if (formats[f].size < 0) {
        const char *p = formats[f].fields;
        int total = 0;

        while (*p != '\0') {
            char ftype[64];
            int count = 1;
            int sz;
            const char *br;
            size_t len = strcspn(p, " ");

            if (len >= sizeof ftype)
                return -1;
            memcpy(ftype, p, len);
            ftype[len] = '\0';
            br = strchr(ftype, '[');
            if (br != NULL) {
                count = atoi(br + 1);
                ftype[br - ftype] = '\0';
            }
            sz = type_size(ftype);
            if (sz < 0)
                return -1;
            total += sz * count;
            p += strcspn(p, ";");
            if (*p == ';')
                p++;
        }
        formats[f].size = total;
    }
    return formats[f].size;
}

static field_ref_t find_field(int fmt, const char *field)
{
    field_ref_t ref = { NO_FIELD, "", 1 };
    const char *p = formats[fmt].fields;
    int offset = 0;

    while (*p != '\0') {
        char ftype[64];
        char fname[64];
        int count = 1;
        char *br;

        if (sscanf(p, "%63s %63[^;]", ftype, fname) != 2)
            break;
        br = strchr(ftype, '[');
        if (br != NULL) {
            count = atoi(br + 1);
            *br = '\0';
        }
        if (strcmp(fname, field) == 0) {
            ref.offset = offset;
            memcpy(ref.type, ftype, sizeof ref.type);
            ref.count = count;
            return ref;
        }
        offset += type_size(ftype) * count;
        p += strcspn(p, ";");
        if (*p == ';')
            p++;
    }
    return ref;
}

static void add_format(const char *def, size_t len)
{
    const char *colon = memchr(def, ':', len);
    size_t nlen;

    if (colon == NULL || n_formats == MAX_FORMATS)
        return;
    nlen = (size_t)(colon - def);
    formats[n_formats].name = calloc(nlen + 1, 1);
    formats[n_formats].fields = calloc(len - nlen, 1);
    if (formats[n_formats].name == NULL || formats[n_formats].fields == NULL) {
        fprintf(stderr, "ulog2csv: out of memory\n");
        exit(EXIT_FAILURE);
    }
    memcpy(formats[n_formats].name, def, nlen);
    memcpy(formats[n_formats].fields, colon + 1, len - nlen - 1);
    formats[n_formats].size = -1;
    n_formats++;
}

typedef struct {
    int fmt;
    field_ref_t ts, lat, lon, alt, hdop, sats, fix, jam, spoof;
    double lat_scale, alt_scale;
} gps_layout_t;

static int gps_layout(gps_layout_t *g, int fmt)
{
    g->fmt = fmt;
    g->ts = find_field(fmt, "timestamp");
    g->hdop = find_field(fmt, "hdop");
    g->sats = find_field(fmt, "satellites_used");
    g->fix = find_field(fmt, "fix_type");
    g->jam = find_field(fmt, "jamming_state");
    g->spoof = find_field(fmt, "spoofing_state");
    g->lat = find_field(fmt, "latitude_deg");
    g->lat_scale = 1.0;
    g->alt_scale = 1.0;
    if (g->lat.offset != NO_FIELD) {
        g->lon = find_field(fmt, "longitude_deg");
        g->alt = find_field(fmt, "altitude_msl_m");
    } else {
        /* pre-v1.15 messages: int32 lat/lon in 1e-7 deg, alt in mm */
        g->lat = find_field(fmt, "lat");
        g->lon = find_field(fmt, "lon");
        g->alt = find_field(fmt, "alt");
        g->lat_scale = 1e-7;
        g->alt_scale = 1e-3;
    }
    return g->ts.offset != NO_FIELD && g->lat.offset != NO_FIELD &&
           g->lon.offset != NO_FIELD && g->alt.offset != NO_FIELD &&
           g->hdop.offset != NO_FIELD && g->sats.offset != NO_FIELD &&
           g->fix.offset != NO_FIELD;
}

int main(int argc, char **argv)
{
    static uint8_t buf[65536];
    static const uint8_t magic[7] = { 'U', 'L', 'o', 'g', 0x01, 0x12, 0x35 };
    FILE *fp;
    gps_layout_t gps;
    field_ref_t sat_count = { NO_FIELD, "", 1 };
    field_ref_t sat_snr = { NO_FIELD, "", 1 };
    field_ref_t sat_used = { NO_FIELD, "", 1 };
    int sat_fmt = -1;
    int baro_fmt = -1;
    field_ref_t baro_alt = { NO_FIELD, "", 1 };
    double baro_latest = NAN;
    FILE *truth = NULL;
    double cn0_latest = NAN;
    double t0 = -1.0;
    long next_second = 0;
    unsigned long rows = 0;
    unsigned long jam_unknown = 0, jam_alert = 0, spoof_unknown = 0, spoof_alert = 0;
    size_t i;

    if (argc != 2 && argc != 3) {
        fprintf(stderr, "usage: %s log.ulg [truth.csv] > frames.csv\n", argv[0]);
        return EXIT_FAILURE;
    }
    fp = fopen(argv[1], "rb");
    if (fp == NULL) {
        perror(argv[1]);
        return EXIT_FAILURE;
    }
    if (fread(buf, 1, 16, fp) != 16 || memcmp(buf, magic, sizeof magic) != 0) {
        fprintf(stderr, "ulog2csv: %s: not a ULog file\n", argv[1]);
        fclose(fp);
        return EXIT_FAILURE;
    }
    memset(&gps, 0, sizeof gps);
    gps.fmt = -1;
    for (i = 0; i < MAX_MSG_IDS; i++)
        sub_format[i] = -1;

    printf("t_s,lat_deg,lon_deg,alt_m,cn0_dbhz,hdop,sats\n");
    if (argc == 3) {
        truth = fopen(argv[2], "w");
        if (truth == NULL) {
            perror(argv[2]);
            fclose(fp);
            return EXIT_FAILURE;
        }
        fprintf(truth, "t_s,baro_alt_m,jamming_state,spoofing_state\n");
    }

    for (;;) {
        uint8_t hdr[3];
        uint16_t size;

        if (fread(hdr, 1, 3, fp) != 3)
            break;
        size = (uint16_t)(hdr[0] | hdr[1] << 8);
        if (fread(buf, 1, size, fp) != size)
            break;

        if (hdr[2] == 'F') {
            add_format((const char *)buf, size);
        } else if (hdr[2] == 'A' && size > 3) {
            uint16_t msg_id = (uint16_t)(buf[1] | buf[2] << 8);
            char name[128];
            size_t nlen = (size_t)(size - 3) < sizeof name - 1 ? (size_t)(size - 3) : sizeof name - 1;
            int fmt;

            memcpy(name, buf + 3, nlen);
            name[nlen] = '\0';
            fmt = find_format(name);
            sub_format[msg_id] = (int16_t)fmt;
            sub_multi[msg_id] = buf[0];
            if (fmt >= 0 && buf[0] == 0 && gps.fmt < 0 &&
                (strcmp(name, "vehicle_gps_position") == 0) && !gps_layout(&gps, fmt)) {
                fprintf(stderr, "ulog2csv: unsupported %s layout\n", name);
                fclose(fp);
                return EXIT_FAILURE;
            }
            if (fmt >= 0 && buf[0] == 0 && strcmp(name, "satellite_info") == 0) {
                sat_fmt = fmt;
                sat_count = find_field(fmt, "count");
                sat_snr = find_field(fmt, "snr");
                sat_used = find_field(fmt, "used");
            }
            if (fmt >= 0 && buf[0] == 0 && strcmp(name, "vehicle_air_data") == 0) {
                baro_fmt = fmt;
                baro_alt = find_field(fmt, "baro_alt_meter");
            }
        } else if (hdr[2] == 'D' && size >= 2) {
            uint16_t msg_id = (uint16_t)(buf[0] | buf[1] << 8);
            const uint8_t *d = buf + 2;
            int fmt = sub_format[msg_id];

            if (fmt < 0 || sub_multi[msg_id] != 0)
                continue;

            if (fmt == baro_fmt && baro_alt.offset != NO_FIELD) {
                baro_latest = rd_scalar(d + baro_alt.offset, baro_alt.type);
                continue;
            }
            if (fmt == sat_fmt && sat_count.offset != NO_FIELD && sat_snr.offset != NO_FIELD) {
                unsigned int n = d[sat_count.offset];
                unsigned int k, used = 0;
                double sum = 0.0;

                if (n > (unsigned int)sat_snr.count)
                    n = (unsigned int)sat_snr.count;
                for (k = 0; k < n; k++) {
                    if (sat_used.offset != NO_FIELD && d[sat_used.offset + (int)k] == 0)
                        continue;
                    sum += d[sat_snr.offset + (int)k];
                    used++;
                }
                if (used > 0)
                    cn0_latest = sum / used;
            } else if (fmt == gps.fmt) {
                double t = rd_scalar(d + gps.ts.offset, gps.ts.type) * 1e-6;
                double hdop;

                if (rd_scalar(d + gps.fix.offset, gps.fix.type) < MIN_FIX_TYPE)
                    continue;
                if (t0 < 0.0)
                    t0 = t;
                if (t - t0 < (double)next_second)
                    continue;
                next_second = (long)(t - t0) + 1;
                printf("%.1f,%.7f,%.7f,%.2f,",
                       (double)(long)(t - t0),
                       rd_scalar(d + gps.lat.offset, gps.lat.type) * gps.lat_scale,
                       rd_scalar(d + gps.lon.offset, gps.lon.type) * gps.lat_scale,
                       rd_scalar(d + gps.alt.offset, gps.alt.type) * gps.alt_scale);
                if (isnan(cn0_latest))
                    printf("nan,");
                else
                    printf("%.1f,", cn0_latest);
                hdop = rd_scalar(d + gps.hdop.offset, gps.hdop.type);
                /* some receiver drivers leave HDOP at 0 rather than reporting it */
                if (hdop > 0.0)
                    printf("%.2f,", hdop);
                else
                    printf("nan,");
                printf("%u\n", (unsigned int)rd_scalar(d + gps.sats.offset, gps.sats.type));
                rows++;

                {
                    /* PX4 GNSS state enums: 0 unknown, 1 ok/none, >= 2 receiver-declared alert */
                    unsigned int jam = (gps.jam.offset != NO_FIELD) ? d[gps.jam.offset] : 0u;
                    unsigned int spoof = (gps.spoof.offset != NO_FIELD) ? d[gps.spoof.offset] : 0u;

                    jam_unknown += (jam == 0);
                    jam_alert += (jam >= 2);
                    spoof_unknown += (spoof == 0);
                    spoof_alert += (spoof >= 2);
                    if (truth != NULL) {
                        fprintf(truth, "%.1f,", (double)(long)(t - t0));
                        if (isnan(baro_latest))
                            fprintf(truth, "nan,");
                        else
                            fprintf(truth, "%.2f,", baro_latest);
                        fprintf(truth, "%u,%u\n", jam, spoof);
                    }
                }
            }
        }
    }
    fclose(fp);
    if (truth != NULL)
        fclose(truth);

    if (gps.fmt < 0)
        fprintf(stderr, "ulog2csv: %s: no vehicle_gps_position topic\n", argv[1]);
    else if (sat_fmt < 0)
        fprintf(stderr, "ulog2csv: %s: no satellite_info topic, C/N0 written as nan\n", argv[1]);
    fprintf(stderr, "ulog2csv: %s: %lu frames\n", argv[1], rows);
    fprintf(stderr, "ulog2csv: receiver jamming_state  alert %lu, unknown %lu\n",
            jam_alert, jam_unknown);
    fprintf(stderr, "ulog2csv: receiver spoofing_state alert %lu, unknown %lu\n",
            spoof_alert, spoof_unknown);
    return rows > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
