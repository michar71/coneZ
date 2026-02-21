// nmea.cpp — Minimal NMEA-0183 parser (pure C, no Arduino dependency)
// Handles $G?RMC, $G?GGA, $G?GSA from any GNSS talker ID.

#include "nmea.h"
#include <string.h>
#include <stdlib.h>

// Sentence types (internal)
enum { NMEA_UNKNOWN = 0, NMEA_RMC = 1, NMEA_GGA = 2, NMEA_GSA = 3 };

// Knots to m/s conversion factor
static const float KNOTS_TO_MPS = 0.514444f;

// ---------- helpers ----------

// Parse hex digit, -1 on invalid
static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Parse NMEA latitude/longitude: DDMM.MMMMM or DDDMM.MMMMM → degrees
static double parse_degrees(const char *term) {
    if (term[0] == '\0') return 0.0;
    // Find the decimal point
    const char *dot = strchr(term, '.');
    int int_len = dot ? (int)(dot - term) : (int)strlen(term);
    // Integer part: everything before the dot
    long int_part = strtol(term, NULL, 10);
    // Degrees = first (int_len-2) digits, minutes = last 2 digits + fraction
    long deg = int_part / 100;
    long min_int = int_part % 100;
    // Fractional minutes
    double min_frac = 0.0;
    if (dot) {
        // Parse digits after '.' manually for full precision
        double mult = 0.1;
        for (const char *p = dot + 1; *p >= '0' && *p <= '9'; p++) {
            min_frac += (*p - '0') * mult;
            mult *= 0.1;
        }
    }
    return (double)deg + ((double)min_int + min_frac) / 60.0;
}

// Parse decimal integer from term (e.g. "123" → 123), 0 if empty
static long parse_int(const char *term) {
    if (term[0] == '\0') return 0;
    return strtol(term, NULL, 10);
}

// Parse decimal float from term
static float parse_float(const char *term) {
    if (term[0] == '\0') return 0.0f;
    return strtof(term, NULL);
}

// Parse HDOP: "1.20" → 120 (hundredths), matching TinyGPSPlus convention
static int parse_hdop(const char *term) {
    if (term[0] == '\0') return 0;
    // Parse as float then convert to hundredths
    float val = strtof(term, NULL);
    return (int)(val * 100.0f + 0.5f);
}

// ---------- term processing ----------

// Identify sentence type from first term (e.g. "GPRMC", "GNGGA", "GPGSA")
static uint8_t identify_sentence(const char *term) {
    int len = strlen(term);
    if (len < 5) return NMEA_UNKNOWN;
    // Last 3 chars identify the sentence type
    const char *suffix = term + len - 3;
    if (strcmp(suffix, "RMC") == 0) return NMEA_RMC;
    if (strcmp(suffix, "GGA") == 0) return NMEA_GGA;
    if (strcmp(suffix, "GSA") == 0) return NMEA_GSA;
    return NMEA_UNKNOWN;
}

// Process a completed term
static void process_term(nmea_data_t *d) {
    if (d->term_num == 0) {
        d->sentence_type = identify_sentence(d->term);
        return;
    }

    switch (d->sentence_type) {
    case NMEA_RMC:
        // $G?RMC,hhmmss.ss,status,lat,N/S,lon,E/W,speed,course,ddmmyy,,,mode
        //         1         2      3   4   5   6   7     8      9       12
        switch (d->term_num) {
        case 1: { // Time: hhmmss.ss
            long t = parse_int(d->term);
            d->s_hour   = t / 10000;
            d->s_minute = (t / 100) % 100;
            d->s_second = t % 100;
            d->s_time_valid = (d->term[0] != '\0');
            break;
        }
        case 2: // Status: A=active, V=void
            d->s_has_fix = (d->term[0] == 'A');
            break;
        case 3: // Latitude
            d->s_lat = parse_degrees(d->term);
            d->s_location_set = (d->term[0] != '\0');
            break;
        case 4: // N/S
            if (d->term[0] == 'S') d->s_lat = -d->s_lat;
            break;
        case 5: // Longitude
            d->s_lon = parse_degrees(d->term);
            break;
        case 6: // E/W
            if (d->term[0] == 'W') d->s_lon = -d->s_lon;
            break;
        case 7: // Speed over ground (knots)
            d->s_speed = parse_float(d->term) * KNOTS_TO_MPS;
            break;
        case 8: // Course over ground (degrees)
            d->s_course = parse_float(d->term);
            break;
        case 9: { // Date: ddmmyy
            long dt = parse_int(d->term);
            d->s_day   = dt / 10000;
            d->s_month = (dt / 100) % 100;
            int yy     = dt % 100;
            d->s_year  = yy + 2000;
            d->s_date_valid = (d->term[0] != '\0');
            break;
        }
        }
        break;

    case NMEA_GGA:
        // $G?GGA,hhmmss.ss,lat,N/S,lon,E/W,quality,numSV,HDOP,alt,M,...
        //         1        2   3   4   5   6       7     8    9  10
        switch (d->term_num) {
        case 1: { // Time
            long t = parse_int(d->term);
            d->s_hour   = t / 10000;
            d->s_minute = (t / 100) % 100;
            d->s_second = t % 100;
            d->s_time_valid = (d->term[0] != '\0');
            break;
        }
        case 2: // Latitude
            d->s_lat = parse_degrees(d->term);
            d->s_location_set = (d->term[0] != '\0');
            break;
        case 3: // N/S
            if (d->term[0] == 'S') d->s_lat = -d->s_lat;
            break;
        case 4: // Longitude
            d->s_lon = parse_degrees(d->term);
            break;
        case 5: // E/W
            if (d->term[0] == 'W') d->s_lon = -d->s_lon;
            break;
        case 6: // Fix quality (0=invalid, 1=GPS, 2=DGPS, ...)
            d->s_has_fix = (d->term[0] != '0' && d->term[0] != '\0');
            break;
        case 7: // Number of satellites
            d->s_satellites = parse_int(d->term);
            break;
        case 8: // HDOP
            d->s_hdop = parse_hdop(d->term);
            break;
        case 9: // Altitude (meters)
            d->s_alt = parse_float(d->term);
            d->s_alt_valid = (d->term[0] != '\0');
            break;
        }
        break;

    case NMEA_GSA:
        // $G?GSA,mode,fixtype,sv1..sv12,PDOP,HDOP,VDOP
        //         1    2      3..14      15   16   17
        switch (d->term_num) {
        case 2: // Fix type: 1=no fix, 2=2D, 3=3D
            d->fix_type = parse_int(d->term);
            break;
        case 15: // PDOP
            d->pdop = parse_float(d->term);
            break;
        case 16: // HDOP (from GSA — separate from GGA HDOP)
            break; // We use GGA HDOP which is more standard
        case 17: // VDOP
            d->vdop = parse_float(d->term);
            break;
        }
        break;
    }
}

// Commit staged fields to output after checksum validates
static bool commit_sentence(nmea_data_t *d) {
    switch (d->sentence_type) {
    case NMEA_RMC:
        if (d->s_location_set) {
            d->lat = d->s_lat;
            d->lon = d->s_lon;
            d->speed = d->s_speed;
            d->course = d->s_course;
            d->location_valid = d->s_has_fix;
            d->update_count++;
        }
        if (d->s_time_valid) {
            d->hour = d->s_hour;
            d->minute = d->s_minute;
            d->second = d->s_second;
            d->time_valid = true;
        }
        if (d->s_date_valid) {
            d->day = d->s_day;
            d->month = d->s_month;
            d->year = d->s_year;
            d->date_valid = true;
        }
        return true;

    case NMEA_GGA:
        if (d->s_location_set) {
            d->lat = d->s_lat;
            d->lon = d->s_lon;
            d->location_valid = d->s_has_fix;
            d->update_count++;
        }
        d->satellites = d->s_satellites;
        d->hdop = d->s_hdop;
        if (d->s_alt_valid) {
            d->alt = d->s_alt;
            d->altitude_valid = true;
        }
        if (d->s_time_valid) {
            d->hour = d->s_hour;
            d->minute = d->s_minute;
            d->second = d->s_second;
            d->time_valid = true;
        }
        return true;

    case NMEA_GSA:
        // GSA fields (fix_type, pdop, vdop) are committed directly in process_term
        return true;

    default:
        return false;
    }
}

// ---------- public API ----------

void nmea_init(nmea_data_t *d) {
    memset(d, 0, sizeof(*d));
}

bool nmea_encode(nmea_data_t *d, char c) {
    // Start of new sentence
    if (c == '$') {
        d->term_pos = 0;
        d->term_num = 0;
        d->term[0] = '\0';
        d->parity = 0;
        d->in_checksum = false;
        d->checksum_chars = 0;
        d->checksum_val = 0;
        d->sentence_type = NMEA_UNKNOWN;
        // Clear staging
        d->s_lat = 0; d->s_lon = 0;
        d->s_alt = 0; d->s_speed = 0; d->s_course = 0;
        d->s_satellites = 0; d->s_hdop = 0;
        d->s_hour = 0; d->s_minute = 0; d->s_second = 0;
        d->s_day = 0; d->s_month = 0; d->s_year = 0;
        d->s_has_fix = false;
        d->s_date_valid = false;
        d->s_time_valid = false;
        d->s_alt_valid = false;
        d->s_location_set = false;
        return false;
    }

    // Ignore until we see a '$'
    if (d->sentence_type == NMEA_UNKNOWN && d->term_num == 0 && d->term_pos == 0 && !d->in_checksum)
        return false;

    // End of sentence
    if (c == '\r' || c == '\n') {
        if (d->in_checksum && d->checksum_chars == 2) {
            // Validate checksum
            if (d->parity == d->checksum_val) {
                return commit_sentence(d);
            }
        }
        // Reset for next sentence
        d->term_pos = 0;
        d->term_num = 0;
        d->sentence_type = NMEA_UNKNOWN;
        return false;
    }

    // Checksum hex digits after '*'
    if (d->in_checksum) {
        int h = hex_digit(c);
        if (h >= 0 && d->checksum_chars < 2) {
            d->checksum_val = (d->checksum_val << 4) | h;
            d->checksum_chars++;
        }
        return false;
    }

    // Start of checksum
    if (c == '*') {
        // Process final term
        d->term[d->term_pos] = '\0';
        process_term(d);
        d->in_checksum = true;
        d->checksum_chars = 0;
        d->checksum_val = 0;
        return false;
    }

    // Accumulate parity (everything between '$' and '*')
    d->parity ^= (uint8_t)c;

    // Field separator
    if (c == ',') {
        d->term[d->term_pos] = '\0';
        process_term(d);
        d->term_num++;
        d->term_pos = 0;
        return false;
    }

    // Accumulate character into current term
    if (d->term_pos < NMEA_MAX_TERM - 1) {
        d->term[d->term_pos++] = c;
    }

    return false;
}
