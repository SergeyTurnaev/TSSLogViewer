#ifndef LOGDESCRIPTIONWIDGET_H
#define LOGDESCRIPTIONWIDGET_H

#include <QWidget>
#include <QVersionNumber>
#include <QDateTime>
#include "nlohmann/json.hpp"

namespace Ui {
class LogDescriptionWidget;
}

class LogDescriptionWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LogDescriptionWidget(QWidget *parent = nullptr);
    ~LogDescriptionWidget();

    void setError();
    void setJsonMetadata(nlohmann::json metadata);
    QVersionNumber hardwareVersion(int ver) const;
    QVersionNumber firmwareVersion(int ver) const;
    QVersionNumber bootloaderVersion(int ver) const;
    QVersionNumber escVersion(int ver) const;
    QDateTime creationTime(int date, int time) const;

protected:
    void changeEvent(QEvent *event) override;

private slots:
    void openMap();

private:
    float m_latitude = 0;
    float m_longitude = 0;
    Ui::LogDescriptionWidget *ui;
};

#endif // LOGDESCRIPTIONWIDGET_H
