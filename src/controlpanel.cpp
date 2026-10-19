#include "controlpanel.h"
#include "ui_controlpanel.h"
#include <QHeaderView>
#include <QPushButton>
#include <QScrollBar>

ControlPanel::ControlPanel(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ControlPanel)
{
    ui->setupUi(this);
    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
}

ControlPanel::~ControlPanel()
{
    delete ui;
}

void verticalResizeTableViewToContents(QTableWidget *table) {
    int rowTotalHeight = 0;

    int rowCount = table->verticalHeader()->count();
    for (int i = 0; i < rowCount; ++i) {
        if (!table->verticalHeader()->isSectionHidden(i)) {
            rowTotalHeight += table->verticalHeader()->sectionSize(i);
        }
    }

    if (!table->horizontalScrollBar()->isHidden()) {
        rowTotalHeight += table->horizontalScrollBar()->height();
    }

    if (!table->horizontalHeader()->isHidden()) {
        rowTotalHeight += table->horizontalHeader()->height();
    }

    int frameWidth = table->frameWidth() * 2;

    table->setMinimumHeight(rowTotalHeight + frameWidth);
}

void ControlPanel::addStatistics(const QString &name, const QColor color, const Statistic &statistic)
{
    int newCol = ui->tableWidget->columnCount();
    ui->tableWidget->insertColumn(newCol);
    ui->tableWidget->horizontalHeader()->setFixedHeight(0);

    auto button = new QPushButton(name);
    button->setFlat(false);
    button->setCheckable(true);
    button->setChecked(false);
    connect(button, &QPushButton::clicked, this, [this, name](bool checked) {
        setEnable(name, checked);
        emit toggled(name, checked);
    });

    if (name == "Time") {
        button->setDisabled(true);
    }

    auto headerItem = new QTableWidgetItem();
    headerItem->setFlags(headerItem->flags() & ~Qt::ItemIsEditable);
    ui->tableWidget->setItem(0, newCol, headerItem);
    ui->tableWidget->setCellWidget(0, newCol, button);

    for (int i = 0; i < STATISTIC_FIELDS_COUNT; i++) {
        auto item = new QTableWidgetItem();
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        ui->tableWidget->setItem(i + 1, newCol, item);
    }

    int minCellWidth = ui->tableWidget->fontMetrics().horizontalAdvance("000.000XX")
                       + ui->tableWidget->fontMetrics().horizontalAdvance("00");
    ui->tableWidget->setColumnWidth(newCol, qMax(button->sizeHint().width(), minCellWidth));

    updateStatisics(name, statistic);
    disable(name);

    verticalResizeTableViewToContents(ui->tableWidget);
}

void ControlPanel::removeStatistics(const QString &name)
{
    for (int column = 0; column < ui->tableWidget->columnCount(); column++) {
        auto *btn = qobject_cast<QPushButton *>(ui->tableWidget->cellWidget(0, column));
        if (!btn || btn->text() != name) {
            continue;
        }
        ui->tableWidget->removeColumn(column);
        column--;
    }
}

void ControlPanel::updateTimeRange(const QTime from, const QTime to)
{
    m_startTime = from;
    m_endTime = to;
}

void ControlPanel::updateStatisics(const QString &name, const Statistic &statistic)
{
    m_stats[name] = statistic;
    for (int column = 0; column < ui->tableWidget->columnCount(); column++) {
        auto *btn = qobject_cast<QPushButton *>(ui->tableWidget->cellWidget(0, column));
        if (!btn || btn->text() != name) {
            continue;
        }
        if (btn->text() == tr("Time", "As the name of the time row")) {
            ui->tableWidget->item(1, column)->setText(QString("%1s %2ms").arg(int(statistic.min)).arg(int(statistic.min*1000) % 1000));
            ui->tableWidget->item(2, column)->setText("-");
            ui->tableWidget->item(3, column)->setText(QString("%1s %2ms").arg(int(statistic.delta)).arg(int(statistic.delta*1000) % 1000));
            ui->tableWidget->item(4, column)->setText(QString("%1s %2ms").arg(int(statistic.max)).arg(int(statistic.max*1000) % 1000));
            ui->tableWidget->item(5, column)->setText(QString("%1s %2ms").arg(int(statistic.cur)).arg(int(statistic.cur*1000) % 1000));
        }
        else {
            ui->tableWidget->item(1, column)->setText(QString::number(statistic.min));
            ui->tableWidget->item(2, column)->setText(QString::number(statistic.avg));
            ui->tableWidget->item(3, column)->setText(QString::number(statistic.delta));
            ui->tableWidget->item(4, column)->setText(QString::number(statistic.max));
            ui->tableWidget->item(5, column)->setText(QString::number(statistic.cur));
        }
    }
}

void ControlPanel::updateCurrentValue(const QString &name, double val)
{
    if (m_stats.keys().contains(name)) {
        m_stats[name].cur = val;
        updateStatisics(name, m_stats[name]);
    }
}

bool ControlPanel::isEnabled(const QString &name)
{
    for (int column = 0; column < ui->tableWidget->columnCount(); column++) {
        auto *btn = qobject_cast<QPushButton *>(ui->tableWidget->cellWidget(0, column));
        if (!btn || btn->text() != name) {
            continue;
        }
        return btn->isChecked();
    }
    return false;
}

void ControlPanel::enable(const QString &name)
{
    setEnable(name, true);
}

void ControlPanel::disable(const QString &name)
{
    setEnable(name, false);
}

void ControlPanel::setEnable(const QString &name, bool enable)
{
    for (int column = 0; column < ui->tableWidget->columnCount(); column++) {
        auto *btn = qobject_cast<QPushButton *>(ui->tableWidget->cellWidget(0, column));
        if (!btn || btn->text() != name) {
            continue;
        }
        if (!enable) {
            for (int row = 0; row < STATISTIC_FIELDS_COUNT; row++) {
                ui->tableWidget->item(row + 1, column)->setText("-");
            }
        }
        if (btn->isChecked() != enable) {
            btn->setChecked(enable);
            emit toggled(name, enable);
        }
    }
}

void ControlPanel::clear()
{
//    ui->tableWidget->setColumnCount(1);
//    ui->tableWidget->setRowCount(1);
    m_stats.clear();
}

void ControlPanel::changeEvent(QEvent *event)
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
