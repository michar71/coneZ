// nmea.h — Minimal NMEA-0183 parser (pure C, no Arduino dependency)
// Parses RMC, GGA, GSA sentences from any GNSS talker ID.

#ifndef _conez_nmea_h
#define _conez_nmea_h

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NMEA_MAX_TERM 16   // longest field we care about (DDMM.MMMMM)

typedef struct {
    // --- Parsed output fields ---
    double lat, lon;           // degrees, signed (S/W negative)
    float alt;                 // meters above MSL
    float speed;               // m/s (converted from knots)
    float course;              // degrees true
    int satellites;            // from GGA field 7
    int hdop;                  // hundredths (e.g. 120 = 1.20) from GGA
    int fix_type;              // from GSA: 0=unknown, 1=no fix, 2=2D, 3=3D
    float pdop, vdop;          // from GSA
    uint8_t hour, minute, second;
    uint8_t day, month;
    uint16_t year;

    bool location_valid;       // RMC status 'A' or GGA quality > 0
    bool date_valid;
    bool time_valid;
    bool altitude_valid;
    uint32_t update_count;     // incremented on each valid location commit

    // --- Internal parser state ---
    char     term[NMEA_MAX_TERM]; // current term being accumulated
    uint8_t  term_pos;            // write position in term[]
    uint8_t  term_num;            // which field (0 = sentence ID)
    uint8_t  sentence_type;       // 0=unknown, 1=RMC, 2=GGA, 3=GSA
    uint8_t  parity;              // running XOR checksum
    bool     in_sentence;         // between '$' and CR/LF
    bool     in_checksum;         // past the '*'
    uint8_t  checksum_chars;      // how many hex digits read after '*'
    uint8_t  checksum_val;        // parsed checksum value

    // Staging area: fields committed only after checksum passes
    double   s_lat, s_lon;
    float    s_alt, s_speed, s_course;
    int      s_satellites, s_hdop;
    uint8_t  s_hour, s_minute, s_second;
    uint8_t  s_day, s_month;
    uint16_t s_year;
    bool     s_has_fix;          // RMC status='A' or GGA quality>0
    bool     s_date_valid;
    bool     s_time_valid;
    bool     s_alt_valid;
    bool     s_location_set;     // at least one position field parsed
} nmea_data_t;

// Initialize parser state. Call once before feeding characters.
void nmea_init(nmea_data_t *d);

// Feed one character from the NMEA stream.
// Returns true when a complete, checksum-validated sentence was committed.
bool nmea_encode(nmea_data_t *d, char c);

#ifdef __cplusplus
}
#endif

#endif
