#ifndef SENSOR_PANEL_H
#define SENSOR_PANEL_H

#include <QScrollArea>
#include <functional>

class QVBoxLayout;

class SensorPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit SensorPanel(QWidget *parent = nullptr);

private:
    void addFloatSlider(QVBoxLayout *layout, const QString &label,
                        float min, float max, float def, int decimals,
                        std::function<void(float)> setter);
    void addIntSlider(QVBoxLayout *layout, const QString &label,
                      int min, int max, int def,
                      std::function<void(int)> setter);
};

#endif
