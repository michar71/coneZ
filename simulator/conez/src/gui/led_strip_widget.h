#ifndef LED_STRIP_WIDGET_H
#define LED_STRIP_WIDGET_H

#include <QWidget>
#include <QTimer>
#include <vector>
#include "led_state.h"

class LedStripWidget : public QWidget {
    Q_OBJECT
public:
    explicit LedStripWidget(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void refresh();

private:
    QTimer m_timer;
    std::vector<std::vector<RGB>> m_snapshot;
};

#endif
