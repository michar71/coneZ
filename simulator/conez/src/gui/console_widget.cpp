#include "console_widget.h"
#include <QVBoxLayout>
#include <QFont>
#include <QMouseEvent>

ConsoleWidget::ConsoleWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(1000);

    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);
    m_output->setFont(mono);

    QPalette pal = m_output->palette();
    pal.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
    pal.setColor(QPalette::Text, QColor(0xd4, 0xd4, 0xd4));
    m_output->setPalette(pal);

    m_input = new QLineEdit(this);
    m_input->setFont(mono);
    m_input->setPlaceholderText("Enter command (e.g. run /path/to/test.bas)");

    QPalette ipal = m_input->palette();
    ipal.setColor(QPalette::Base, QColor(0x25, 0x25, 0x25));
    ipal.setColor(QPalette::Text, QColor(0xd4, 0xd4, 0xd4));
    m_input->setPalette(ipal);

    layout->addWidget(m_output, 1);
    layout->addWidget(m_input);

    connect(m_input, &QLineEdit::returnPressed, this, &ConsoleWidget::onReturn);

    m_output->installEventFilter(this);
    m_output->viewport()->installEventFilter(this);
}

bool ConsoleWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress &&
        (obj == m_output || obj == m_output->viewport())) {
        m_input->setFocus();
        return false;  // still allow text selection
    }
    return QWidget::eventFilter(obj, event);
}

void ConsoleWidget::appendText(const QString &text)
{
    m_output->moveCursor(QTextCursor::End);
    m_output->insertPlainText(text);
    m_output->moveCursor(QTextCursor::End);
}

void ConsoleWidget::onReturn()
{
    QString cmd = m_input->text().trimmed();
    if (cmd.isEmpty()) return;

    appendText("> " + cmd + "\n");
    m_input->clear();
    emit commandEntered(cmd);
}
