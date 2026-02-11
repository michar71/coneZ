#ifndef LED_STATE_H
#define LED_STATE_H

#include <cstdint>
#include <vector>
#include <mutex>
#include <atomic>

struct RGB {
    uint8_t r = 0, g = 0, b = 0;
};

class LedState {
public:
    LedState();

    // WASM thread writes
    void setPixel(int channel, int pos, uint8_t r, uint8_t g, uint8_t b);
    void fill(int channel, uint8_t r, uint8_t g, uint8_t b);
    void show();
    int count(int channel) const;

    void setBuffer(int channel, const uint8_t *rgb_data, int cnt);
    void shift(int channel, int amount, uint8_t r, uint8_t g, uint8_t b);
    void rotate(int channel, int amount);
    void reverse(int channel);

    // GUI thread reads — snapshot for painting
    bool isDirty() const { return m_dirty.load(); }
    void clearDirty() { m_dirty.store(false); }
    std::vector<std::vector<RGB>> snapshot();

    // Resize (from config)
    void resize(int c1, int c2, int c3, int c4);

private:
    std::vector<RGB> &buf(int channel);
    const std::vector<RGB> &buf(int channel) const;

    mutable std::mutex m_mutex;
    std::atomic<bool> m_dirty{false};
    std::vector<RGB> m_channels[4];
};

LedState &ledState();

#endif
