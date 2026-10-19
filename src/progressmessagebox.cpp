#include <QApplication>
#include <QProgressBar>
#include <QVBoxLayout>
#include "progressmessagebox.h"

ProgressMessageBox::ProgressMessageBox()
    : QDialog(nullptr, Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint)
{
    m_progress = new QProgressBar(this);
    m_progress->setValue(0);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_progress);
    setMinimumWidth(360);
    show();
    qApp->processEvents();
}

void ProgressMessageBox::setMaximum(int value)
{
    m_progress->setMaximum(value);
}

void ProgressMessageBox::setMinumum(int value)
{
    m_progress->setMinimum(value);
}

void ProgressMessageBox::setValue(int value)
{
    m_progress->setValue(value);
    qApp->processEvents();
}

int ProgressMessageBox::value()
{
    return m_progress->value();
}

void ProgressMessageBox::changeEvent(QEvent *event)
{
    if(event) {
        switch(event->type()) {
        case QEvent::LanguageChange:
            // update any dynamic elements
            break;
        default:
            break;
        }
    }

    QDialog::changeEvent(event);
}
