#include "openlogwidget.h"
#include "ui_openlogwidget.h"

OpenLogWidget::OpenLogWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OpenLogWidget)
{
    ui->setupUi(this);
}

OpenLogWidget::~OpenLogWidget()
{
    delete ui;
}

void OpenLogWidget::changeEvent(QEvent *event)
{
    if(event) {
        switch(event->type()) {
        case QEvent::LanguageChange:
            ui->retranslateUi(this);
            break;
        default:
            break;
        }
    }
    QWidget::changeEvent(event);
}
