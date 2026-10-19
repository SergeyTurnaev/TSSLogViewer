#include "logdescriptionwidget.h"
#include "ui_logdescriptionwidget.h"
#include <QDesktopServices>
#include <QEvent>
#include <QUrl>

LogDescriptionWidget::LogDescriptionWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::LogDescriptionWidget)
{
    ui->setupUi(this);
    connect(ui->pbOpenMap, &QPushButton::clicked, this, &LogDescriptionWidget::openMap);
}

LogDescriptionWidget::~LogDescriptionWidget()
{
    delete ui;
}

void LogDescriptionWidget::setError()
{
    ui->leBootloaderVersion->clear();
    ui->leEscVersion->clear();
    ui->leFwVersion->clear();
    ui->leHWVersion->clear();
    ui->leCoordinates->clear();
    ui->leCreatedAt->clear();
    ui->leLogFreqHZ->clear();
    ui->leMotorPoles->clear();
    ui->leSettings->clear();
    ui->leEnergyRefj->clear();
    ui->leUser->clear();
    ui->leCreatedAt->setText(tr("Error reading log file"));
}

static int bcdToDecimal(unsigned int bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

QDateTime bcdDateTimeToQDateTime(unsigned int bcdDate, unsigned int bcdTime) {
    // В формате журнала год хранится во втором байте даты.
    int year = bcdToDecimal((bcdDate >> 8) & 0xFF);
    int month = bcdToDecimal((bcdDate >> 16) & 0xFF);
    int day = bcdToDecimal((bcdDate >> 24) & 0xFF);

    int hour = bcdToDecimal(bcdTime >> 24);
    int minute = bcdToDecimal((bcdTime >> 16) & 0xFF);
    int second = bcdToDecimal((bcdTime >> 8) & 0xFF);

    return QDateTime(QDate(year, month, day), QTime(hour, minute, second));
}

QDateTime LogDescriptionWidget::creationTime(int date, int time) const
{
    return bcdDateTimeToQDateTime(date, time);
}

QVersionNumber LogDescriptionWidget::hardwareVersion(int ver) const
{
    return QVersionNumber(ver >> 8,
                          (ver & 0xF0) >> 4,
                          ver & 0x0F);
}

QVersionNumber LogDescriptionWidget::firmwareVersion(int ver) const
{
    return QVersionNumber(ver >> 8,
                          ver & 0xFF);
}

QVersionNumber LogDescriptionWidget::bootloaderVersion(int ver) const
{
    return QVersionNumber(ver >> 8, ver & 0xFF);
}

QVersionNumber LogDescriptionWidget::escVersion(int ver) const
{
    return QVersionNumber(ver >> 8, ver  & 0xFF);
}

void LogDescriptionWidget::setJsonMetadata(nlohmann::json metadata)
{
    try {
        ui->leUser->setText(QString("%1 [%2-%3]").
                arg(QString::fromStdString(metadata["userName"]),
                    QString::fromStdString(metadata["userRegNum"]["country"])).
                arg(metadata["userRegNum"]["num"].get<int>()));

        double latitude = metadata["latitude"];
        double longitude = metadata["longitude"];
        m_latitude = latitude;
        m_longitude = longitude;
        if(latitude == 4294967296 || longitude == 4294967296) {
            ui->leCoordinates->setEnabled(false);
            ui->pbOpenMap->setEnabled(false);
            ui->leCoordinates->setText("");
        }
        else {
            ui->leCoordinates->setEnabled(true);
            ui->pbOpenMap->setEnabled(true);
            ui->leCoordinates->setText(QString("[ %1; %2 ]").arg(latitude, 0, 'f', 4).arg(longitude, 0, 'f', 4));
        }
        QDateTime dateTime = creationTime(metadata["date"].get<int>(), metadata["time"].get<int>());
        QString dateTimeString;
        if (dateTime.isValid()) {
            dateTimeString = dateTime.toString();
        }
        else {
            dateTimeString = "Unknown";
        }
        ui->leCreatedAt->setText(dateTimeString);
        ui->leSettings->setText(QString::fromStdString(metadata["settSetName"]));
        ui->leMotorPoles->setText(QString::number(metadata["motorPoles"].get<int>()));
        ui->leEnergyRefj->setText(QString("%1 Wh").arg(metadata["energyRefJ"].get<int>() / 3600));
        ui->leLogFreqHZ->setText(QString("%1 Hz").arg(metadata["loggerFreqHz"].get<int>()));
        ui->leHWVersion->setText(hardwareVersion(metadata["hwVersion"].get<int>()).toString());
        ui->leFwVersion->setText(firmwareVersion(metadata["fwVersion"].get<int>()).toString());
        ui->leBootloaderVersion->setText(bootloaderVersion(metadata["blVersion"].get<int>()).toString());

        if((metadata["hwVersion"].get<int>() & 0xF000) == 0)
        {
            ui->leEscVersion->setEnabled(false);
            ui->lblESCVersion->setEnabled(false);
            ui->leEscVersion->setText("");
        }
        else
        {
            ui->leEscVersion->setText(escVersion(metadata["escVersion"].get<int>()).toString());
            ui->leEscVersion->setEnabled(true);
            ui->lblESCVersion->setEnabled(true);
        }
    }
    catch(...){
        ui->leUser->setText("File is corrupted or unsupported");
    }
}

void LogDescriptionWidget::openMap()
{
    QString link = QString("https://www.google.com/maps/@%1,%2,16z?entry=ttu").arg(m_latitude, 0, 'f', 7).arg(m_longitude, 0, 'f', 7);
    QDesktopServices::openUrl(QUrl(link));
}

void LogDescriptionWidget::changeEvent(QEvent *event)
{
    if(event) {
        switch(event->type()) {
        case QEvent::LanguageChange:
            ui->retranslateUi(this);
        break;
        default:
            QWidget::changeEvent(event);
        }
    }
}
