#include "led_strip_widget.h"
#include "artnet_sender.h"
#include <QPainter>

LedStripWidget::LedStripWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    connect(&m_timer, &QTimer::timeout, this, &LedStripWidget::refresh);
    m_timer.start(33); // ~30 FPS
}

void LedStripWidget::refresh()
{
    if (ledState().isDirty()) {
        m_snapshot = ledState().snapshot();
        ledState().clearDirty();
        update();
        artnetSender().sendFrame(m_snapshot);
    }
}

void LedStripWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);

    if (m_snapshot.empty()) return;

    int numChannels = (int)m_snapshot.size();
    int rowHeight = height() / numChannels;
    if (rowHeight < 4) rowHeight = 4;

    for (int ch = 0; ch < numChannels; ch++) {
        const auto &strip = m_snapshot[ch];
        int count = (int)strip.size();
        if (count == 0) continue;

        int y = ch * rowHeight;
        int gap = 1;
        float pixW = (float)(width() - gap) / count;
        if (pixW < 2) { pixW = 2; gap = 0; }

        // Channel label
        p.setPen(QColor(100, 100, 100));
        p.drawText(2, y + 12, QString("CH%1").arg(ch + 1));

        for (int i = 0; i < count; i++) {
            int x = (int)(i * pixW) + gap;
            int w = (int)((i + 1) * pixW) - (int)(i * pixW) - gap;
            if (w < 1) w = 1;

            const RGB &c = strip[i];
            p.fillRect(x, y + 16, w, rowHeight - 18, QColor(c.r, c.g, c.b));
        }
    }
}
