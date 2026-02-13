#ifndef CUE_ENGINE_H
#define CUE_ENGINE_H

#include <QObject>
#include <QTimer>
#include <QString>
#include <cstdint>
#include <vector>
#include <functional>
#include <atomic>
#include <cstring>

// ---------- File format constants ----------

#define CUE_MAGIC 0x43554530    // "CUE0"

// Cue types
#define CUE_TYPE_STOP     0
#define CUE_TYPE_EFFECT   1
#define CUE_TYPE_FILL     2
#define CUE_TYPE_BLACKOUT 3
#define CUE_TYPE_GLOBAL   4

// Spatial modes
#define SPATIAL_NONE              0
#define SPATIAL_RADIAL_CONFIG     1
#define SPATIAL_RADIAL_ABSOLUTE   2
#define SPATIAL_RADIAL_RELATIVE   3
#define SPATIAL_DIR_CONFIG        4
#define SPATIAL_DIR_ABSOLUTE      5
#define SPATIAL_DIR_RELATIVE      6

// Flags
#define CUE_FLAG_FIRE_FORGET  0x01
#define CUE_FLAG_LOOP         0x02
#define CUE_FLAG_BLEND_ADD    0x04

// ---------- Binary structs (must match firmware) ----------

#pragma pack(push, 1)

struct cue_header {
    uint32_t magic;             //  4  "CUE0"
    uint16_t version;           //  2  format version
    uint16_t num_cues;          //  2  number of cue entries
    uint16_t record_size;       //  2  sizeof(cue_entry) at authoring time
    uint8_t  reserved[54];      // 54  future use
};  // 64 bytes

struct cue_entry {
    // identity (4 bytes)
    uint8_t  cue_type;          //  1  see CUE_TYPE_*
    uint8_t  channel;           //  1  LED channel 1-4
    uint16_t group;             //  2  see group targeting
    // timing (8 bytes)
    uint32_t start_ms;          //  4  offset from music start
    uint32_t duration_ms;       //  4  0 = instantaneous
    // spatial (16 bytes)
    float    spatial_delay;     //  4  ms per meter
    float    spatial_param1;    //  4  lat or north_m
    float    spatial_param2;    //  4  lon or east_m
    uint16_t spatial_angle;     //  2  compass bearing (degrees)
    uint8_t  spatial_mode;      //  1  see SPATIAL_*
    uint8_t  flags;             //  1  see CUE_FLAG_*
    // effect (36 bytes)
    char     effect_file[20];   // 20  e.g. "/shows/fire.wasm"
    uint8_t  params[16];        // 16  effect-specific parameters
};  // 64 bytes

#pragma pack(pop)

static_assert(sizeof(cue_header) == 64, "cue_header must be 64 bytes");
static_assert(sizeof(cue_entry) == 64, "cue_entry must be 64 bytes");

// ---------- Geo helpers ----------

struct GeoResult {
    float distance;
    float bearing_deg;
};

void latlonToMeters(float lat_deg, float lon_deg, float *x, float *y);
GeoResult xyToPolar(float x1, float y1, float x2, float y2);

// ---------- CueEngine ----------

class CueEngine : public QObject {
    Q_OBJECT
public:
    explicit CueEngine(QObject *parent = nullptr);

    bool load(const QString &path);
    void start(qint64 offsetMs = 0);
    void stop();

    bool isPlaying() const { return m_playing.load(); }
    qint64 elapsedMs() const;
    int cueCount() const { return (int)m_cues.size(); }
    int cueCursor() const { return m_cursor; }
    QString loadedFile() const { return m_loadedFile; }

    void setOutputCallback(std::function<void(const QString&)> cb) { m_output = cb; }

private slots:
    void tick();

private:
    bool cueMatches(uint16_t group) const;
    int32_t computeSpatialOffset(const cue_entry *cue) const;
    void dispatchCue(const cue_entry *cue);
    void output(const QString &msg);

    std::vector<cue_entry> m_cues;
    int m_cursor = 0;
    std::atomic<bool> m_playing{false};
    std::atomic<qint64> m_startEpochMs{0};

    // Precomputed cone position in meter-space
    float m_myX = 0, m_myY = 0;
    float m_originX = 0, m_originY = 0;

    QTimer m_timer;
    QString m_loadedFile;
    std::function<void(const QString&)> m_output;
};

CueEngine &cueEngine();

#endif
