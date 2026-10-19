
#include "mainwindow.h"

#include <QApplication>
#include <QIcon>
#include <QSettings>
#include <QTranslator>
#include <QLibraryInfo>
#ifdef Q_OS_WIN
#include <QFileInfo>
#include <QRegularExpression>
#endif
#include <memory>
#include <list>

#include "version.h"

static QIcon loadAppIcon()
{
    QIcon icon;
    for (int size : {64, 128, 256, 512}) {
        icon.addFile(QStringLiteral(":/appicon-%1.png").arg(size), QSize(size, size));
    }
    return icon;
}

#ifdef Q_OS_WIN
static QString fileAssociationCommand()
{
    const QString appPath = QCoreApplication::applicationFilePath().replace('/', '\\');
    return '"' + appPath + QStringLiteral("\" \"%1\"");
}

static void migrateLegacyFileAssociation()
{
    const QString appCommand = fileAssociationCommand();
    QSettings installedAssociation(
        QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Classes\\TSSLogViewer.tsslog\\shell\\open\\command"),
        QSettings::Registry64Format);
    if (installedAssociation.value(QStringLiteral(".")).toString().compare(appCommand, Qt::CaseInsensitive) != 0) {
        return;
    }

    // Ассоциациями управляет установщик. Здесь переносится только команда старой
    // пользовательской ассоциации, которая может перекрывать установленную для всех.
    // Миграция выполняется от имени пользователя, запустившего установленный просмотрщик.
    QSettings legacyAssociation(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\TSSLogViewer\\shell\\open\\command"),
        QSettings::Registry64Format);
    if (!legacyAssociation.contains(QStringLiteral("."))) {
        return;
    }

    const QString legacyCommand = legacyAssociation.value(QStringLiteral(".")).toString();
    const QRegularExpression commandPattern(QStringLiteral(R"command(\A"([^"\r\n]+)" "%1"\z)command"));
    const auto match = commandPattern.match(legacyCommand);
    if (!match.hasMatch()) {
        return;
    }

    const QFileInfo legacyExecutable(match.captured(1));
    if (!legacyExecutable.isAbsolute()
        || legacyExecutable.fileName().compare(QStringLiteral("TSSLogViewer.exe"), Qt::CaseInsensitive) != 0) {
        return;
    }

    if (legacyCommand.compare(appCommand, Qt::CaseInsensitive) != 0) {
        legacyAssociation.setValue(QStringLiteral("."), appCommand);
    }
}

static void removeLegacyFileAssociation()
{
    QSettings legacyAssociation(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\TSSLogViewer\\shell\\open\\command"),
        QSettings::Registry64Format);
    if (legacyAssociation.value(QStringLiteral(".")).toString().compare(fileAssociationCommand(), Qt::CaseInsensitive) != 0) {
        return;
    }

    // При удалении программы или отключении ассоциации убираем только значения,
    // оставшиеся от миграции на эту копию. Настройки других копий и UserChoice сохраняем.
    legacyAssociation.remove(QStringLiteral("."));
    legacyAssociation.sync();
    if (legacyAssociation.status() != QSettings::NoError) {
        return;
    }

    QSettings extensionAssociation(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\.tsslog"),
        QSettings::Registry64Format);
    if (extensionAssociation.value(QStringLiteral(".")).toString().compare(QStringLiteral("TSSLogViewer"), Qt::CaseInsensitive) == 0) {
        extensionAssociation.remove(QStringLiteral("."));
    }
}
#endif

std::list<std::shared_ptr<QTranslator>> setupTranslators() {
    QSettings settings;

    auto translator = std::make_shared<QTranslator>();
    auto qtTranslator = std::make_shared<QTranslator>();

    QLocale locale = QLocale::system();
    QString systemLanguageCode = locale.name().section('_', 0, 0);
    QString languageCode = settings.value("language", systemLanguageCode).toString();

    QStringList supportedLocales = QString(SUPPORTED_LANGUAGES).split(" ");

    if (!supportedLocales.contains(languageCode)) {
        languageCode = "en";
    }

    if (qtTranslator->load("qtbase_" + languageCode,
                           QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        qApp->installTranslator(qtTranslator.get());
    }

    QString translationFile = ":/ts/app_" + languageCode + ".qm";
    if (!translator->load(translationFile)) {
        translator->load(":/ts/app_en.qm");
    }
    qApp->installTranslator(translator.get());

    return { qtTranslator, translator };
}

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName(VER_COMPANYNAME_STR);
    QCoreApplication::setApplicationName(VER_PRODUCTNAME_STR);
    QCoreApplication::setApplicationVersion(VER_PRODUCTVERSION_STR);
    QApplication a(argc, argv);

#ifdef Q_OS_WIN
    const QStringList arguments = a.arguments();
    if (arguments.size() == 2 && arguments.at(1) == QStringLiteral("--remove-legacy-file-association")) {
        removeLegacyFileAssociation();
        return 0;
    }

    migrateLegacyFileAssociation();
    if (arguments.size() == 2 && arguments.at(1) == QStringLiteral("--migrate-file-association")) {
        return 0;
    }
#endif

    a.setWindowIcon(loadAppIcon());
    auto translators = setupTranslators();

    MainWindow w(translators);
    if (a.arguments().size() > 1) {
        QString filePath = a.arguments().at(1);
        w.openLog(filePath);
    }

    w.show();
    return a.exec();
}
