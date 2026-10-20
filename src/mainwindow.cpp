
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QFileDialog>
#include <QAbstractItemView>
#include <QStyle>
#include <QTimer>
#include <QLibraryInfo>
#include "Zippy/Zippy.hpp"
#include "plotview.h"
#include "updatecontroller.h"

void MainWindow::setupLocale()
{
    std::map<QString, QString> langCodeToName {
        { "en", "English" },
        { "ru", "Русский" },
        { "de", "Deutsch" },
        { "zh_TW", "中國人"},
        { "it", "Italiano" },
        { "fr", "Français" },
        { "es", "Español" },
        { "ro", "Română" },
        { "pl", "Polski" },
        { "ja", "日本語" },
        { "cs", "Čeština" }
    };

    QStringList supportedLanguages = QString(SUPPORTED_LANGUAGES).split(" ");

    QComboBox *langBox = new QComboBox();
    langBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    langBox->setMaxVisibleItems(supportedLanguages.size());
    for (const auto& lang : supportedLanguages) {
        if(langCodeToName[lang].isEmpty())
            langCodeToName[lang] = lang;
        langBox->addItem(QIcon(QString(":/res/") + lang + ".png"),
                         langCodeToName[lang]);
        langBox->setItemData(langBox->count() - 1, lang);
    }
    // Полоса прокрутки не должна отнимать место у названий языков.
    langBox->view()->setMinimumWidth(langBox->sizeHint().width()
        + langBox->style()->pixelMetric(QStyle::PM_ScrollBarExtent));
#ifdef Q_OS_MACOS
    ui->menubar->setNativeMenuBar(false);
#endif
    ui->menubar->setCornerWidget(langBox);

    connect(langBox, &QComboBox::currentTextChanged, this, [this, langBox]() {
        QString lang = langBox->currentData().toString();

        for(auto &translator : m_translators) {
            qApp->removeTranslator(translator.get());
        }
        m_translators.clear();

        auto translator = std::make_shared<QTranslator>();
        auto qtTranslator = std::make_shared<QTranslator>();

        QStringList supportedLanguages = QString(SUPPORTED_LANGUAGES).split(" ");

        if (!supportedLanguages.contains(lang)) {
            lang = "en";
        }

        if (!qtTranslator->load("qtbase_" + lang,
                                QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
            qtTranslator->load("qtbase_en",
                               QLibraryInfo::path(QLibraryInfo::TranslationsPath));
        }

        if (!translator->load(":/ts/app_" + lang + ".qm")) {
            translator->load(":/ts/app_en.qm");
        }

        qApp->installTranslator(translator.get());
        qApp->installTranslator(qtTranslator.get());

        m_translators.push_back(translator);
        m_translators.push_back(qtTranslator);

        QSettings settings;
        settings.setValue("language", lang);
    });

    QSettings settings;
    QLocale systemLocale = QLocale::system();
    QString systemLanguage = systemLocale.name().section('_', 0, 0);
    QString language = settings.value("language", systemLanguage).toString();
    langBox->setCurrentText(langCodeToName[language]);
}

MainWindow::MainWindow(std::list<std::shared_ptr<QTranslator>> translators, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow), m_translators(translators)
{
    ui->setupUi(this);

    setupLocale();

    new UpdateController(this, ui->actionApplicationUpdates, [] { return QApplication::activeModalWidget() == nullptr; });

    QSettings settings;
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("state").toByteArray());

    QString appName = QCoreApplication::applicationName();
    QString appVersion = QCoreApplication::applicationVersion();
    QString orgName = QCoreApplication::organizationName();
    QString buildDate = QString(__DATE__);
    QString windowTitle = QString("%1  v %2 (%3) [%4]").arg(appName, appVersion, buildDate, orgName);
    setWindowTitle(windowTitle);

    ui->plots->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    ui->pbOpenFolder->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
    ui->pbRefreshFolder->setIcon(QApplication::style()->standardIcon(QStyle::SP_BrowserReload));

    ui->lwLogs->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    ui->splitter->setSizes({width() - 300, 300});
    ui->splitter->setStretchFactor(0, 666);

    connect(ui->pbOpenFolder, &QPushButton::clicked, this, &MainWindow::selectDirectory);
    connect(ui->pbRefreshFolder, &QPushButton::clicked, this, [this]() {
        scanDirectory(QDir().fromNativeSeparators(ui->leCurrentFolder->text()));
    });
    connect(ui->lwLogs, &QTableWidget::itemSelectionChanged, this, &MainWindow::logSelected);
    connect(ui->lwLogs, &QTableWidget::doubleClicked, this, &MainWindow::logOpened);
    connect(ui->actionOpenFile, &QAction::triggered, this, &MainWindow::openFile);
    connect(&m_logWatcher, &QFileSystemWatcher::fileChanged, this, &MainWindow::logDirectoryChanged);
    connect(&m_logWatcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::logDirectoryChanged);
    connect(ui->plots, &QTabWidget::tabCloseRequested, this, &MainWindow::closePlotTab);
    connect(ui->actionExit, &QAction::triggered, qApp, &QApplication::exit);

    QSettings sets("TSSRacing", "TSSLimiter");
    QString lastDirectory = sets.value("logs_dir", "").toString();
    if(lastDirectory.isEmpty()) {
        lastDirectory = QString("%1/TSSLogger/logs").arg(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    }
    ui->leCurrentFolder->setText(QDir().toNativeSeparators(lastDirectory));
    scanDirectory(lastDirectory);
}

MainWindow::~MainWindow()
{
    QSettings settings;
    settings.setValue("geometry", saveGeometry());
    settings.setValue("state", saveState());
    delete ui;
}

void MainWindow::openLog(QString filename)
{
    PlotView *view = new PlotView(filename);
    int idx = ui->plots->addTab(view, view->name());
    ui->plots->setCurrentIndex(idx);
}

int bcdToDecimal(unsigned int bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static QDateTime bcdDateTimeToQDateTime(unsigned int bcdDate, unsigned int bcdTime) {
    // who the fuck invented setting the year to the 2nd byte?
    int year = bcdToDecimal((bcdDate >> 8) & 0xFF) + 2000;
    int month = bcdToDecimal((bcdDate >> 16) & 0xFF);
    int day = bcdToDecimal((bcdDate >> 24) & 0xFF);

    int hour = bcdToDecimal(bcdTime >> 24);
    int minute = bcdToDecimal((bcdTime >> 16) & 0xFF);
    int second = bcdToDecimal((bcdTime >> 8) & 0xFF);

    return QDateTime(QDate(year, month, day), QTime(hour, minute, second));
}

static QDateTime creationTime(int date, int time)
{
    return bcdDateTimeToQDateTime(date, time);
}

void MainWindow::openFile()
{
    QSettings sets("TSSRacing", "TSSLimiter");
    QString lastDirectory = sets.value("logs_dir", "").toString();
    if(lastDirectory.isEmpty()) {
        lastDirectory = QString("%1/TSSLogger/logs").arg(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    }
    QString filename = QFileDialog::getOpenFileName(this, tr("Please select log file"), lastDirectory, "*.tsslog");

    if(filename.isEmpty()) {
        return;
    }

    PlotView *view = new PlotView(filename);
    int idx = ui->plots->addTab(view, view->name());
    ui->plots->setCurrentIndex(idx);

    sets.setValue("logs_dir", lastDirectory);
}

void MainWindow::scanDirectory(QString directory)
{
    m_logWatcher.removePaths(m_logWatcher.directories());
    ui->lwLogs->setRowCount(0);
    m_files.clear();

    if (directory.endsWith('/') || directory.endsWith('\\')) {
        directory = directory.left(directory.length() - 1);
    }

    QSettings("TSSRacing", "TSSLimiter").setValue("logs_dir", directory);

    rescanDirectory(directory);

    // Set column proportions: 3:5:5:3:5
    int totalWidth = ui->lwLogs->width(); // Total width of the table
    int columnProportions[] = {3, 5, 5, 3, 5};
    for (int i = 0; i < ui->lwLogs->columnCount(); ++i) {
        int width = (totalWidth * columnProportions[i]) / 21; // Adjust the denominator as needed
        ui->lwLogs->setColumnWidth(i, width);
        ui->lwLogs->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
    }

    m_logWatcher.addPath(directory);
}

void MainWindow::rescanDirectory(QString directory)
{
    QDirIterator it(directory, QStringList() << "*.tsslog", QDir::Files, QDirIterator::Subdirectories);

    QStringList files = m_files.keys();
    while (it.hasNext()) {
        QString filePath = it.next();
        if(m_files.contains(filePath)) {
            files.removeAll(filePath);
            continue;
        }
        processLogFile(directory, filePath);
    }
    for(const QString &file : files) {
        for(int row = 0; row < ui->lwLogs->rowCount(); row++) {
            QString rowFile = ui->lwLogs->item(row, 0)->data(Qt::UserRole).toString();
            if(file == rowFile) {
                ui->lwLogs->removeRow(row);
                m_files.remove(file);
                break;
            }
        }
    }
}

void MainWindow::processLogFile(QString baseDir, QString filename)
{
#ifdef _WIN32
    Zippy::ZipArchive archive(filename.toLocal8Bit().toStdString());
#else
    Zippy::ZipArchive archive(filename.toStdString());
#endif
    if(!archive.IsOpen()) {
        return;
    }
    if(!archive.HasEntry("metadata.json")) {
        return;
    }
    if(!archive.HasEntry("data.json")) {
        return;
    }
    auto metadataJSON = archive.GetEntry("metadata.json").GetDataAsString();
    if(metadataJSON.empty()) {
        return;
    }

    auto metadata = nlohmann::json::parse(metadataJSON);

    QString subdir = QFileInfo(filename).path().replace(baseDir, "");
    if (!subdir.isEmpty()) {
        subdir += "/";
    }

    try {
        QString deviceName = tr("No Name", "The name of the device for the log");
        if(metadata.contains("deviceName")) {
            deviceName = QString::fromStdString(metadata["deviceName"]);
        }
        QString name = QString("%1%2 (%3-%4): %5").
                        arg(subdir).
                        arg(deviceName).
                        arg(QString::fromStdString(metadata["userRegNum"]["country"])).
                        arg(metadata["userRegNum"]["num"].get<int>()).
                        arg(metadata["head"]["sessionNumber"].get<int>());

        if (metadata["head"]["dotsCount"].get<int>() == 0xFFFFFFFF) {
            name.append(tr(" (not complete race)", "Added to the log name if the log is not complete"));
        }
        QTableWidgetItem *nameCell = new QTableWidgetItem(name);
        nameCell->setData(Qt::UserRole, filename);

        QString user = QString::fromStdString(metadata["userName"]);
        QTableWidgetItem *userCell = new QTableWidgetItem(user);

        QString settings = QString::fromStdString(metadata["settSetName"]);
        QTableWidgetItem *settingsCell = new QTableWidgetItem(settings);

        QDateTime timestamp = creationTime(metadata["date"].get<int>(), metadata["time"].get<int>());
        if(!timestamp.isValid() || timestamp.date().year() < 2022) {
            timestamp = QFileInfo(filename).lastModified();
        }

        QTableWidgetItem *timestampCell = new QTableWidgetItem(timestamp.toString("yyyy/MM/dd hh:mm:ss"));

        int rowCount = ui->lwLogs->rowCount();
        m_files[filename] = rowCount;
        ui->lwLogs->insertRow(rowCount);

        ui->lwLogs->setItem(rowCount, 0, nameCell);
        ui->lwLogs->setItem(rowCount, 1, userCell);
        ui->lwLogs->setItem(rowCount, 2, settingsCell);
        ui->lwLogs->setItem(rowCount, 3, timestampCell);
    } catch (...) {
        int rowCount = ui->lwLogs->rowCount();
        ui->lwLogs->insertRow(rowCount);

        ui->lwLogs->setItem(rowCount, 0, new QTableWidgetItem("File corrupted"));
        ui->lwLogs->setItem(rowCount, 1, new QTableWidgetItem(subdir));
        ui->lwLogs->setItem(rowCount, 2, new QTableWidgetItem(QFileInfo(filename).fileName()));
    }

}

void MainWindow::selectDirectory()
{
    QSettings sets("TSSRacing", "TSSLimiter");
    QString lastDirectory = sets.value("logs_dir", "").toString();
    if(lastDirectory.isEmpty()) {
        lastDirectory = QString("%1/TSSLogger/logs").arg(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    }
    QString dirname = QFileDialog::getExistingDirectory(this, tr("Please select log file"), lastDirectory, QFileDialog::ReadOnly);

    if(dirname.isEmpty()) {
        return;
    }
    ui->leCurrentFolder->setText(QDir().toNativeSeparators(dirname));
    scanDirectory(dirname);
}

void MainWindow::resizeColumnsToContents()
{
    ui->lwLogs->resizeColumnsToContents();
}

void MainWindow::logSelected()
{
    if(ui->lwLogs->selectedItems().isEmpty()) {
        return;
    }

    QString filename = ui->lwLogs->selectedItems().first()->data(Qt::UserRole).toString();
    if(filename.isEmpty()) {
        return;
    }

#ifdef _WIN32
    Zippy::ZipArchive archive(filename.toLocal8Bit().toStdString());
#else
    Zippy::ZipArchive archive(filename.toStdString());
#endif
    if(!archive.IsOpen()) {
        return;
    }
    if(!archive.HasEntry("metadata.json")) {
        return;
    }
    if(!archive.HasEntry("data.json")) {
        return;
    }
    auto metadataJSON = archive.GetEntry("metadata.json").GetDataAsString();
    if(metadataJSON.empty()) {
        return;
    }

    auto metadata = nlohmann::json::parse(metadataJSON);
    ui->logDescription->setJsonMetadata(metadata);
}

void MainWindow::logOpened()
{
    QString filename = ui->lwLogs->selectedItems().first()->data(Qt::UserRole).toString();
    if(filename.isEmpty()) {
        return;
    }

    openLog(filename);
}

void MainWindow::logDirectoryChanged(QString path)
{
    QFileInfo info(path);
    qDebug() << info.isFile() << info.exists() << path;
    if (info.isFile()) {
    if(info.exists()) {
        if(!m_logWatcher.files().contains(path)) {
                m_logWatcher.addPath(path);
            }
            processLogFile(m_logWatcher.directories().first(), path);
        }
    }
    else {
        if (info.exists()) {
            QTimer::singleShot(1000, [this, path]() {rescanDirectory(path);});
        }
        else {

        }
    }
}

void MainWindow::closePlotTab(int idx)
{
    QWidget *view = ui->plots->widget(idx);
    ui->plots->removeTab(idx);
    delete view;
}

void MainWindow::changeEvent(QEvent *event)
{
    if(event) {
        switch(event->type()) {
        case QEvent::LanguageChange: {
            // retranslateUi() resets leCurrentFolder to the empty string from the .ui file,
            // so preserve the current path across the retranslation. LanguageChange is
            // delivered asynchronously, including once right after startup, which is how the
            // path set in the constructor used to get wiped before the window was ever shown.
            const QString currentFolder = ui->leCurrentFolder->text();
            ui->retranslateUi(this);
            ui->leCurrentFolder->setText(currentFolder);
            break;
        }
        default:
            break;
        }
    }
    QMainWindow::changeEvent(event);
}







