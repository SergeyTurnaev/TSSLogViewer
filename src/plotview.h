#ifndef PLOTVIEW_H
#define PLOTVIEW_H

#include <QWidget>
#include "qcustomplot/qcustomplot.h"
#include "nlohmann/json.hpp"
#include "statistic.h"

namespace Ui {
class PlotView;
}

class PlotView : public QWidget
{
    Q_OBJECT

    struct Graph {
        QString name;
        QCPAxis *axis;
        QCPGraph *graph;
        QCPItemTracer *mouseOverTracer;
        QCPItemTracer *rangeFromTracer;
        QCPItemTracer *rangeToTracer;
    };

public:
    explicit PlotView(const QString &filename, QWidget *parent = nullptr);
    ~PlotView();

    QString name() const;

protected:
    void changeEvent(QEvent *event) override;

private:
    Ui::PlotView *ui;
    QVector<Graph> m_graphs;
    QCPItemRect* m_statRect;
    void setupPlotEvents(QCustomPlot *customPlot);
    void plotData(const nlohmann::json &data, const nlohmann::json &parameters);
    Statistic calculateStatistics(QCPGraph *graph, double startKey, double endKey);
    QString m_name;
    
};

#endif // PLOTVIEW_H
