#ifndef CONTROLPANEL_H
#define CONTROLPANEL_H

#include <QWidget>
#include <QMap>
#include <QTime>

#include "statistic.h"

namespace Ui {
class ControlPanel;
}

class ControlPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ControlPanel(QWidget *parent = nullptr);
    ~ControlPanel();

    void addStatistics(const QString &name, const QColor color, const Statistic &statistic);
    void removeStatistics(const QString &name);
    void updateTimeRange(const QTime from, const QTime to);
    void updateStatisics(const QString &name, const Statistic &statistic);
    void updateCurrentValue(const QString &name, double val);
    bool isEnabled(const QString &name);

public slots:
    void enable(const QString &name);
    void disable(const QString &name);
    void setEnable(const QString &name, bool enable);
    void clear();

protected:
    void changeEvent(QEvent *event) override;

signals:
    void toggled(QString name, bool isEnabled);

private:
    Ui::ControlPanel *ui;

    QTime m_startTime;
    QTime m_endTime;
    QMap<QString, Statistic> m_stats;

};

#endif // CONTROLPANEL_H
