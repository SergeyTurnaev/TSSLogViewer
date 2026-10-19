#ifndef PROGRESSMESSAGEBOX_H
#define PROGRESSMESSAGEBOX_H

#include <QDialog>

class QProgressBar;

class ProgressMessageBox : public QDialog
{
public:
    ProgressMessageBox();
    void setMaximum(int value);
    void setMinumum(int value);
    void setValue(int value);
    int value();

protected:
    void changeEvent(QEvent *event) override;

private:
    QProgressBar *m_progress;
};

#endif // PROGRESSMESSAGEBOX_H
