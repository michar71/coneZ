#ifndef CONSOLE_WIDGET_H
#define CONSOLE_WIDGET_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>

class ConsoleWidget : public QWidget {
    Q_OBJECT
public:
    explicit ConsoleWidget(QWidget *parent = nullptr);

    void appendText(const QString &text);

signals:
    void commandEntered(const QString &cmd);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onReturn();

private:
    QPlainTextEdit *m_output;
    QLineEdit *m_input;
};

#endif
