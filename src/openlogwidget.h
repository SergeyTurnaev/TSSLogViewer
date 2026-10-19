#ifndef OPENLOGWIDGET_H
#define OPENLOGWIDGET_H

#include <QWidget>

namespace Ui {
class OpenLogWidget;
}

class OpenLogWidget : public QWidget
{
    Q_OBJECT

public:
    explicit OpenLogWidget(QWidget *parent = nullptr);
    ~OpenLogWidget();

protected:
    void changeEvent(QEvent *event) override;

private:
    Ui::OpenLogWidget *ui;
};

#endif // OPENLOGWIDGET_H
