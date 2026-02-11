#include "led_state.h"
#include "sim_config.h"
#include <algorithm>
#include <cstring>

static LedState s_leds;
LedState &ledState() { return s_leds; }

LedState::LedState()
{
    auto &cfg = simConfig();
    resize(cfg.led_count1, cfg.led_count2, cfg.led_count3, cfg.led_count4);
}

void LedState::resize(int c1, int c2, int c3, int c4)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_channels[0].resize(c1);
    m_channels[1].resize(c2);
    m_channels[2].resize(c3);
    m_channels[3].resize(c4);
    for (auto &ch : m_channels)
        std::fill(ch.begin(), ch.end(), RGB{0, 0, 0});
    m_dirty.store(true);
}

std::vector<RGB> &LedState::buf(int channel)
{
    return m_channels[std::clamp(channel - 1, 0, 3)];
}

const std::vector<RGB> &LedState::buf(int channel) const
{
    return m_channels[std::clamp(channel - 1, 0, 3)];
}

void LedState::setPixel(int channel, int pos, uint8_t r, uint8_t g, uint8_t b)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto &v = buf(channel);
    if (pos >= 0 && pos < (int)v.size())
        v[pos] = {r, g, b};
}

void LedState::fill(int channel, uint8_t r, uint8_t g, uint8_t b)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto &v = buf(channel);
    std::fill(v.begin(), v.end(), RGB{r, g, b});
}

void LedState::show()
{
    m_dirty.store(true);
}

int LedState::count(int channel) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (channel < 1 || channel > 4) return 0;
    return (int)m_channels[channel - 1].size();
}

void LedState::setBuffer(int channel, const uint8_t *rgb_data, int cnt)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto &v = buf(channel);
    int n = std::min(cnt, (int)v.size());
    for (int i = 0; i < n; i++) {
        v[i] = {rgb_data[i * 3], rgb_data[i * 3 + 1], rgb_data[i * 3 + 2]};
    }
}

void LedState::shift(int channel, int amount, uint8_t r, uint8_t g, uint8_t b)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto &v = buf(channel);
    int cnt = (int)v.size();
    if (cnt == 0) return;
    RGB fill_col{r, g, b};
    if (amount > 0) {
        int s = std::min(amount, cnt);
        std::memmove(&v[s], &v[0], (cnt - s) * sizeof(RGB));
        for (int i = 0; i < s; i++) v[i] = fill_col;
    } else if (amount < 0) {
        int s = std::min(-amount, cnt);
        std::memmove(&v[0], &v[s], (cnt - s) * sizeof(RGB));
        for (int i = cnt - s; i < cnt; i++) v[i] = fill_col;
    }
}

void LedState::rotate(int channel, int amount)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto &v = buf(channel);
    int cnt = (int)v.size();
    if (cnt == 0) return;
    int s = amount % cnt;
    if (s < 0) s += cnt;
    if (s == 0) return;
    std::vector<RGB> tmp(v);
    for (int i = 0; i < cnt; i++)
        v[(i + s) % cnt] = tmp[i];
}

void LedState::reverse(int channel)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto &v = buf(channel);
    std::reverse(v.begin(), v.end());
}

std::vector<std::vector<RGB>> LedState::snapshot()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return {m_channels[0], m_channels[1], m_channels[2], m_channels[3]};
}
