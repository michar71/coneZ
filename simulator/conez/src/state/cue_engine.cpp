#include "cue_engine.h"
#include "sim_config.h"
#include "led_state.h"
#include "sensor_state.h"

#include <QFile>
#include <QDateTime>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define EARTH_RADIUS_METERS 6378137.0

// ---------- Geo helpers ----------

void latlonToMeters(float lat_deg, float lon_deg, float *x, float *y)
{
    double lat_rad = lat_deg * (M_PI / 180.0);
    *y = EARTH_RADIUS_METERS * (M_PI / 180.0) * lat_deg;
    *x = EARTH_RADIUS_METERS * cos(lat_rad) * (M_PI / 180.0) * lon_deg;
}

GeoResult xyToPolar(float x1, float y1, float x2, float y2)
{
    GeoResult result;
    float dx = x2 - x1;
    float dy = y2 - y1;
    result.distance = sqrtf(dx * dx + dy * dy);
    float angle_deg = atan2f(dy, dx) * (180.0f / (float)M_PI);
    if (angle_deg < 0.0f)
        angle_deg += 360.0f;
    result.bearing_deg = angle_deg;
    return result;
}

// ---------- Singleton ----------

CueEngine &cueEngine()
{
    static CueEngine instance;
    return instance;
}

// ---------- CueEngine ----------

CueEngine::CueEngine(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(33);  // ~30 Hz
    connect(&m_timer, &QTimer::timeout, this, &CueEngine::tick);
}

void CueEngine::output(const QString &msg)
{
    if (m_output)
        m_output(msg);
}

bool CueEngine::load(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        output(QString("cue: cannot open %1\n").arg(path));
        return false;
    }

    // Read header
    cue_header hdr;
    if (f.read((char *)&hdr, sizeof(hdr)) != sizeof(hdr)) {
        output("cue: header read failed\n");
        return false;
    }

    if (hdr.magic != CUE_MAGIC) {
        output(QString("cue: bad magic 0x%1 (expected 0x%2)\n")
            .arg(hdr.magic, 8, 16, QChar('0'))
            .arg(CUE_MAGIC, 8, 16, QChar('0')));
        return false;
    }

    if (hdr.version != 0) {
        output(QString("cue: unsupported version %1\n").arg(hdr.version));
        return false;
    }

    if (hdr.record_size < sizeof(cue_entry)) {
        output(QString("cue: record_size %1 too small (need %2)\n")
            .arg(hdr.record_size).arg(sizeof(cue_entry)));
        return false;
    }

    if (hdr.num_cues == 0) {
        output("cue: file has 0 cues\n");
        return false;
    }

    // Read entries with stride for forward compatibility
    std::vector<cue_entry> newCues(hdr.num_cues);
    uint16_t skip = hdr.record_size - sizeof(cue_entry);

    for (int i = 0; i < hdr.num_cues; i++) {
        if (f.read((char *)&newCues[i], sizeof(cue_entry)) != sizeof(cue_entry)) {
            output(QString("cue: read failed at entry %1\n").arg(i));
            return false;
        }
        if (skip > 0)
            f.seek(f.pos() + skip);
    }

    m_cues = std::move(newCues);
    m_rt.assign(m_cues.size(), CueRt{0, false});
    m_firedCount = 0;
    m_playing.store(false);
    m_loadedFile = path;

    output(QString("cue: loaded %1 cues from %2\n").arg(m_cues.size()).arg(path));
    return true;
}

void CueEngine::start(qint64 offsetMs)
{
    if (m_cues.empty()) {
        output("cue: no cue file loaded\n");
        return;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_startEpochMs.store(now - offsetMs);

    // Precompute positions in meter-space
    auto sm = sensorState().read();
    latlonToMeters(sm.lat, sm.lon, &m_myX, &m_myY);
    latlonToMeters(simConfig().origin_lat, simConfig().origin_lon, &m_originX, &m_originY);

    // Precompute each cue's effective start (the offset is fixed for the run)
    // and clear the fired flags.
    m_rt.resize(m_cues.size());
    for (size_t i = 0; i < m_cues.size(); i++) {
        int64_t eff = (int64_t)m_cues[i].start_ms + computeSpatialOffset(&m_cues[i]);
        if (eff < 0) eff = 0;
        m_rt[i].effStart = eff;
        m_rt[i].fired = false;
    }
    m_firedCount = 0;

    m_playing.store(true);
    m_timer.start();

    output(QString("cue: playback started (%1 cues)\n").arg(m_cues.size()));
}

void CueEngine::stop()
{
    m_playing.store(false);
    m_timer.stop();
    output("cue: playback stopped\n");
}

qint64 CueEngine::elapsedMs() const
{
    if (!m_playing.load()) return 0;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 start = m_startEpochMs.load();
    return (now > start) ? (now - start) : 0;
}

void CueEngine::tick()
{
    if (!m_playing.load()) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 start = m_startEpochMs.load();
    uint32_t elapsed_ms = (now > start) ? (uint32_t)(now - start) : 0;

    // Fire every cue whose precomputed effective start has arrived and hasn't
    // fired yet (see the CueRt comment) so a spatially-offset cue can't stall
    // the ones after it.
    for (size_t i = 0; i < m_cues.size(); i++) {
        if (m_rt[i].fired) continue;
        if (m_rt[i].effStart > (int64_t)elapsed_ms) continue;  // not yet — keep scanning

        if (cueMatches(m_cues[i].group))
            dispatchCue(&m_cues[i]);
        m_rt[i].fired = true;
        m_firedCount++;
    }

    if (m_firedCount >= (int)m_cues.size()) {
        m_playing.store(false);
        m_timer.stop();
        output(QString("cue: playback complete (%1 cues)\n").arg(m_cues.size()));
    }
}

bool CueEngine::cueMatches(uint16_t group) const
{
    int mode  = group >> 12;
    int value = group & 0x0FFF;
    int id    = simConfig().cone_id;
    int grp   = simConfig().cone_group;

    switch (mode) {
        case 0: return true;
        case 1: return id == value;
        case 2: return grp == value;
        case 3: return (value >> grp) & 1;
        case 4: return id != value;
        case 5: return grp != value;
        case 6: return !((value >> grp) & 1);
        default: return false;
    }
}

int32_t CueEngine::computeSpatialOffset(const cue_entry *cue) const
{
    if (cue->spatial_mode == SPATIAL_NONE) return 0;

    float ox, oy;
    switch (cue->spatial_mode) {
        case SPATIAL_RADIAL_CONFIG:
        case SPATIAL_DIR_CONFIG:
            ox = m_originX;
            oy = m_originY;
            break;
        case SPATIAL_RADIAL_ABSOLUTE:
        case SPATIAL_DIR_ABSOLUTE:
            latlonToMeters(cue->spatial_param1, cue->spatial_param2, &ox, &oy);
            break;
        case SPATIAL_RADIAL_RELATIVE:
        case SPATIAL_DIR_RELATIVE:
            ox = m_originX + cue->spatial_param2;  // east_m
            oy = m_originY + cue->spatial_param1;  // north_m
            break;
        default:
            return 0;
    }

    float dist;
    if (cue->spatial_mode <= SPATIAL_RADIAL_RELATIVE) {
        GeoResult geo = xyToPolar(ox, oy, m_myX, m_myY);
        dist = geo.distance;
    } else {
        float dx = m_myX - ox;
        float dy = m_myY - oy;
        float angle_rad = cue->spatial_angle * ((float)M_PI / 180.0f);
        dist = dx * sinf(angle_rad) + dy * cosf(angle_rad);
    }

    return (int32_t)(dist * cue->spatial_delay);
}

void CueEngine::dispatchCue(const cue_entry *cue)
{
    auto &cfg = simConfig();

    switch (cue->cue_type) {

    case CUE_TYPE_STOP:
        if (cue->channel >= 1 && cue->channel <= 4) {
            ledState().fill(cue->channel, 0, 0, 0);
            ledState().show();
        }
        break;

    case CUE_TYPE_FILL:
        if (cue->channel >= 1 && cue->channel <= 4) {
            ledState().fill(cue->channel, cue->params[0], cue->params[1], cue->params[2]);
            ledState().show();
        }
        break;

    case CUE_TYPE_BLACKOUT:
        ledState().fill(1, 0, 0, 0);
        ledState().fill(2, 0, 0, 0);
        ledState().fill(3, 0, 0, 0);
        ledState().fill(4, 0, 0, 0);
        ledState().show();
        break;

    case CUE_TYPE_EFFECT:
        output(QString("cue: effect dispatch not yet implemented (%1)\n")
            .arg(QString::fromLatin1(cue->effect_file, strnlen(cue->effect_file, 20))));
        break;

    case CUE_TYPE_GLOBAL:
        output("cue: global cue type not yet implemented\n");
        break;

    default:
        output(QString("cue: unknown cue type %1\n").arg(cue->cue_type));
        break;
    }
}
