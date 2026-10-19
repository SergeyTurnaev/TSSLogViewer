#include "plotview.h"
#include "ui_plotview.h"

#include <fstream>
#include <QDebug>
#include <QRandomGenerator>
#include <QSettings>
#include "Zippy/Zippy.hpp"
#include "progressmessagebox.h"

PlotView::PlotView(const QString &filename, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PlotView)
{
    ui->setupUi(this);
    // Set statistic panel height to sizeHint()
    ui->splitter->setSizes({height() - 1, 1});
    // Disable statistic panel stretching
    ui->splitter->setStretchFactor(0, 666);

#ifdef _WIN32
    Zippy::ZipArchive archive(filename.toLocal8Bit().toStdString());
#else
    Zippy::ZipArchive archive(filename.toStdString());
#endif
    if(!archive.IsOpen()) {
        return;
    }
    auto metadataJSON = archive.GetEntry("metadata.json").GetDataAsString();
    if(metadataJSON.empty()) {
        return;
    }

    auto metadata = nlohmann::json::parse(metadataJSON);
    // Access the nested "parameters" list
    auto parameters = metadata["parametersDesc"]["parameters"];

    auto dataJSON = archive.GetEntry("data.json").GetDataAsString();
    auto data = nlohmann::json::parse(dataJSON);

    QString deviceName = "No Name";
    if(metadata.contains("deviceName")) {
        deviceName = QString::fromStdString(metadata["deviceName"]);
    }
    m_name = QString("%1 (%2-%3): %4").
                    arg(deviceName).
                    arg(QString::fromStdString(metadata["userRegNum"]["country"])).
                    arg(metadata["userRegNum"]["num"].get<int>()).
                    arg(metadata["head"]["sessionNumber"].get<int>());

    plotData(data, parameters);
}


PlotView::~PlotView()
{
    delete ui;
}

QString PlotView::name() const
{
    return m_name;
}

void PlotView::changeEvent(QEvent *event)
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


void toggleZoomMode(QCustomPlot *customPlot)
{
    QCPAxisRect *axisRect = customPlot->axisRect(0);
    if(customPlot->selectedPlottables().isEmpty()) {
        axisRect->setRangeDragAxes(customPlot->xAxis, nullptr);
        axisRect->setRangeZoomAxes(customPlot->xAxis, nullptr);
    }
    else {
        QCPAbstractPlottable *plottable = customPlot->selectedPlottables().first();
        QCPAxisRect *axisRect = customPlot->axisRect(0);
        axisRect->setRangeZoomAxes(nullptr, plottable->valueAxis());
        axisRect->setRangeDragAxes(plottable->keyAxis(), plottable->valueAxis());
    }
}

void PlotView::setupPlotEvents(QCustomPlot *customPlot)
{
    bool *range = new bool(false);
    connect(customPlot, &QCustomPlot::selectionChangedByUser, this, [customPlot]() {

        for(auto axis : customPlot->axisRect()->axes()) {
            axis->setSelectedParts(QCPAxis::spNone);
        }

        for (auto plottable : customPlot->selectedPlottables()) {
            plottable->valueAxis()->setSelectedParts(QCPAxis::spAxis | QCPAxis::spTickLabels | QCPAxis::spAxisLabel);
        }
        toggleZoomMode(customPlot);
    });

    connect(customPlot, &QCustomPlot::axisClick, this, [this, customPlot](QCPAxis *axis, QCPAxis::SelectablePart part, QMouseEvent *event) {
        if (axis->axisType() == QCPAxis::atLeft)
        {
            if (axis->selectedParts() != (QCPAxis::spAxis | QCPAxis::spTickLabels | QCPAxis::spAxisLabel)) {
                axis->setSelectedParts(QCPAxis::spAxis | QCPAxis::spTickLabels | QCPAxis::spAxisLabel);
                axis->graphs().first()->setSelection(QCPDataSelection({0, INT_MAX}));
            }
            else {
                axis->setSelectedParts(QCPAxis::spNone);
                axis->graphs().first()->setSelection(QCPDataSelection());
            }
            customPlot->replot();
            toggleZoomMode(customPlot);
        }
    });
    connect(customPlot, &QCustomPlot::mouseMove, this, [this, customPlot, range](QMouseEvent *event) {

        Statistic timeStat{};
        bool needsReplot = false;
        for(int idx = 0; idx < m_graphs.size(); idx++) {
            bool isVisible = ui->statisticsPanel->isEnabled(m_graphs[idx].name);
            if(!isVisible) {
                continue;
            }

            auto &graph = m_graphs[idx];
            double x = customPlot->xAxis->pixelToCoord(event->pos().x());
            double y = graph.mouseOverTracer->position->value();

            if (*range) {
                y = graph.rangeToTracer->position->value();
                graph.rangeToTracer->setGraphKey(x);
                m_statRect->bottomRight->setCoords(x, customPlot->yAxis->range().lower);
                Statistic stat = calculateStatistics(graph.graph, graph.rangeFromTracer->graphKey(), graph.rangeToTracer->graphKey());
                stat.cur = y;
                ui->statisticsPanel->updateStatisics(graph.name, stat);
            }
            else {
                graph.mouseOverTracer->setGraphKey(x);
                ui->statisticsPanel->updateCurrentValue(graph.name, y);
            }
            needsReplot = true;
            timeStat.cur = x;
            timeStat.min = graph.rangeFromTracer->position->key();
            timeStat.max = graph.rangeToTracer->position->key();
            timeStat.delta = timeStat.max - timeStat.min;
        }
        ui->statisticsPanel->updateStatisics("Time", timeStat);
        if (needsReplot) {
            customPlot->replot(QCustomPlot::rpQueuedReplot);
        }
    });
    connect(customPlot, &QCustomPlot::mousePress, [this, customPlot, range](QMouseEvent* event) {
        if (event->button() == Qt::RightButton) {
            // Disable default range drag interaction
            customPlot->setInteractions(customPlot->interactions() & ~QCP::iRangeDrag);
            Statistic timeStat{};
            for(int idx = 0; idx < m_graphs.size(); idx++) {
                bool isVisible = ui->statisticsPanel->isEnabled(m_graphs[idx].name);
                if(!isVisible) {
                    continue;
                }

                auto &graph = m_graphs[idx];
                double x = customPlot->xAxis->pixelToCoord(event->pos().x());
                double y = graph.mouseOverTracer->position->value();

                graph.rangeFromTracer->setGraphKey(x);

                // Set the starting point for the difference rectangle
                m_statRect->topLeft->setCoords(x, customPlot->yAxis->range().upper);
                m_statRect->bottomRight->setCoords(x, customPlot->yAxis->range().lower);
                m_statRect->setVisible(true);
                Statistic stat = calculateStatistics(graph.graph, graph.rangeFromTracer->graphKey(), graph.rangeToTracer->graphKey());
                stat.cur = y;
                ui->statisticsPanel->updateStatisics(graph.name, stat);
                *range = true;
                customPlot->replot();

                timeStat.cur = x;
                timeStat.min = graph.rangeFromTracer->position->key();
                timeStat.max = graph.rangeToTracer->position->key();
                timeStat.delta = timeStat.max - timeStat.min;
            }
            ui->statisticsPanel->updateStatisics("Time", timeStat);
        }
    });
    connect(customPlot, &QCustomPlot::mouseRelease, [this, customPlot, range](QMouseEvent* event) {
        if (event->button() == Qt::RightButton) {
            // Re-enable the range drag interaction after the control-drag is complete
            customPlot->setInteractions(customPlot->interactions() | QCP::iRangeDrag);
        }
        *range = false;
    });

    connect(ui->statisticsPanel, &ControlPanel::toggled, this, [this, customPlot](QString name, bool enabled) {

        // find first already enabled graph, to sync trackers to
        int syncIdx = -1;
        int idx = -1;
        for (int i = 0; i < m_graphs.size(); i++) {
            if (m_graphs[i].graph->visible()) {
                syncIdx = i;
                break;
            }
        }

        for (int i = 0; i < m_graphs.size(); i++) {
            if (m_graphs[i].name == name) {
                idx = i;
                break;
            }
        }

        if (idx == -1) {
            return;
        }

        auto &graph = m_graphs[idx];

        graph.graph->setVisible(enabled);
        graph.axis->setVisible(enabled);
        graph.mouseOverTracer->setVisible(enabled);
        graph.rangeFromTracer->setVisible(enabled);
        graph.rangeToTracer->setVisible(enabled);
        Statistic timeStat{};
        if(enabled) {
            graph.graph->addToLegend();
            if(syncIdx != -1) {
                auto &syncGraph = m_graphs[syncIdx];
                graph.mouseOverTracer->setGraphKey(syncGraph.mouseOverTracer->graphKey());
                graph.rangeFromTracer->setGraphKey(syncGraph.rangeFromTracer->graphKey());
                graph.rangeToTracer->setGraphKey(syncGraph.rangeToTracer->graphKey());
            }
            Statistic stat = calculateStatistics(graph.graph, graph.rangeFromTracer->graphKey(), graph.rangeToTracer->graphKey());
            ui->statisticsPanel->updateStatisics(graph.name, stat);


            timeStat.cur = 0;
            timeStat.min = graph.rangeFromTracer->position->key();
            timeStat.max = graph.rangeToTracer->position->key();
            timeStat.delta = timeStat.max - timeStat.min;
        }
        else {
            graph.graph->removeFromLegend();
        }

        ui->statisticsPanel->updateStatisics("Time", timeStat);
        customPlot->replot();
    });
}

void PlotView::plotData(const nlohmann::json &data, const nlohmann::json &parameters)
{
    auto progress = ProgressMessageBox();
    progress.setValue(5);
    ui->statisticsPanel->clear();

    // Create a QCustomPlot and set it as the central widget
    QCustomPlot *customPlot = ui->plot;
    // 4x MSAA is plenty for line plots; 16x cost a lot of video memory for no visible gain.
    // The GL context is shared process-wide, so this format is applied once, by the first plot.
    customPlot->setOpenGl(true, 4);

    QCPAxisRect *axisRect = customPlot->axisRect(0);
    axisRect->removeAxis(axisRect->axis(QCPAxis::atLeft, 0));


    ui->statisticsPanel->addStatistics("Time", QColor(), {});
    for (int i = 0; i < parameters.size(); ++i) {
        QString parameterName = QString::fromStdString(std::string(parameters[i][0]));
        ui->statisticsPanel->addStatistics(parameterName, QColor(), {});

        QCPAxis::AxisType type = QCPAxis::atLeft;
        QCPAxis *yAxis = axisRect->addAxis(type);
        yAxis->setLabel(parameterName);
        yAxis->setTickLabels(true);
        yAxis->setLabelPadding(-5);
        yAxis->setPadding(20);

        QCPGraph *graph = customPlot->addGraph(axisRect->axis(QCPAxis::atBottom), yAxis);
        graph->setName(parameterName);
        graph->setVisible(false);
        yAxis->setVisible(false);
        graph->removeFromLegend();


        QCPItemTracer* mouseOverTracer = new QCPItemTracer(customPlot);
        mouseOverTracer->setGraph(graph);
        mouseOverTracer->setStyle(QCPItemTracer::tsNone); // No visible tracer

        QCPItemTracer* tracer1 = new QCPItemTracer(customPlot);
        tracer1->setStyle(QCPItemTracer::tsPlus);
        tracer1->setSize(16);
        tracer1->setGraph(graph);
        tracer1->setVisible(false);

        QCPItemTracer* tracer2 = new QCPItemTracer(customPlot);
        tracer2->setStyle(QCPItemTracer::tsPlus);
        tracer2->setSize(16);
        tracer2->setGraph(graph);
        tracer2->setVisible(false);

        m_graphs.append(Graph {
            parameterName, yAxis, graph, mouseOverTracer, tracer1, tracer2
        });
    }


    // Apply a theme to the QCustomPlot
    customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables | QCP::iSelectAxes);
    customPlot->setSelectionTolerance(5);


    // Add a legend
    customPlot->legend->setVisible(true);


    progress.setMaximum(data.size());

    // Iterate over the array of objects
    double time = 0, prevTime = 0;
    for (const auto &value : data) {
        // Get the time value (it should be in seconds for QCPAxisTickerTime)
        time = value["common"]["time_ms"].get<double>() / 1000.0;
        const auto& data = value["data"];

        if (time - prevTime > 1) {
            for (int i = 0; i < data.size(); i++) {
                m_graphs[i].graph->addData(prevTime + 0.001, qQNaN());
            }
        }
        prevTime = time;
        try {
            for (int i = 0; i < data.size(); i++) {
                double fieldValue = data[i];
                m_graphs[i].graph->addData(time, fieldValue);
            }
        }
        catch(...){}
        progress.setValue(progress.value() + 1);
    }

    // Set the ranges and format of the graph's axes
    for (const auto& graph : m_graphs) {
        graph.graph->rescaleValueAxis(true);
        graph.graph->setPen(QPen(QColor(QRandomGenerator::global()->bounded(256), QRandomGenerator::global()->bounded(256), QRandomGenerator::global()->bounded(256)))); // Set a random color for each graph
    }
    m_graphs[0].graph->rescaleKeyAxis();

    // Set the format of the bottom (time) axis
    QSharedPointer<QCPAxisTickerTime> timeTicker(new QCPAxisTickerTime);
    timeTicker->setTimeFormat("%m:%s:%z"); // set the format to MM:SS:ZZZ
    axisRect->axis(QCPAxis::atBottom)->setTicker(timeTicker);
    axisRect->setRangeDragAxes(customPlot->xAxis, nullptr);
    axisRect->setRangeZoomAxes(customPlot->xAxis, nullptr);


    m_statRect = new QCPItemRect(customPlot);
    m_statRect->setVisible(false);  // Initially, hide the rectangle.
    m_statRect->setBrush(QBrush(QColor(200, 200, 255, 80)));  // Semi-transparent blue fill.
    m_statRect->setPen(Qt::NoPen);  // No border.
    m_statRect->setSelectable(false);

    // Force a redraw of the plot
    customPlot->replot();

    setupPlotEvents(customPlot);
    ui->statisticsPanel->enable(m_graphs[0].name);
}

Statistic PlotView::calculateStatistics(QCPGraph *graph, double startKey, double endKey) {
    // Initialization
    double minValue = std::numeric_limits<double>::max();
    double maxValue = std::numeric_limits<double>::lowest();
    double avgValue = 0.0;
    double delta = 0.0;
    int count = 0;

    delta = graph->data()->findEnd(endKey, false)->value - graph->data()->findBegin(startKey, false)->value;

    if (startKey > endKey) {
        std::swap(startKey, endKey);
    }
    // Loop through the data points
    QCPDataContainer<QCPGraphData>::const_iterator it = graph->data()->findBegin(startKey, false);
    QCPDataContainer<QCPGraphData>::const_iterator itEnd = graph->data()->findEnd(endKey, true);

    for (; it != itEnd; ++it) {
        double y = it->value;
        if (std::isnan(y)) {
            continue;
        }
        minValue = qMin(minValue, y);
        maxValue = qMax(maxValue, y);
        avgValue += y;
        count++;
    }

    if (count > 0) {
        avgValue /= count;  // Calculate average
    }

    return {
        minValue,
        maxValue,
        avgValue,
        delta,
    };
}
