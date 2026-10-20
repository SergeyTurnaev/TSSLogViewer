// SPDX-License-Identifier: MIT
#include "updatecontroller.h"
#include "buildconfig.h"
#include "linuxmanifest.h"
#include "linuxinstall.h"
#include "ui_updatedialog.h"
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLockFile>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QPointer>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <stdexcept>

namespace {
QProcessEnvironment cleanEnvironment() {
    auto env = QProcessEnvironment::systemEnvironment();
    // Не передаём окружение смонтированного AppImage вложенному runtime и новой версии.
    for (const auto& key : env.keys())
        if (key == "APPIMAGE" || key == "APPDIR" || key == "ARGV0" || key == "LD_LIBRARY_PATH"
            || key == "LD_PRELOAD" || key.startsWith("QT_")) env.remove(key);
    return env;
}
}
struct UpdateController::Impl {
    UpdateController* owner;
    QWidget* window;
    QDialog* dialog;
    Ui::TSSUpdateDialog ui;
    QNetworkAccessManager network;
    QSettings settings;
    QProcess process;
    QTimer timeout;
    QPointer<QNetworkReply> reply;
    std::function<bool()> canInstall;
    QString original, helper;
    QFileInfo originalInfo;
    QStringList mirrors;
    int mirror = 0;
    bool busy = false, manual = false, ready = false, available = false;
    tss::appimage::Release release;
    std::shared_ptr<QTemporaryDir> staging;
    std::unique_ptr<QLockFile> lock;
    Impl(UpdateController* controller, QWidget* parent, std::function<bool()> guard)
        : owner(controller), window(parent), dialog(new QDialog(parent)), canInstall(std::move(guard)) {}
    QString candidate() const { return staging->filePath("candidate.AppImage"); }
    void show() { dialog->show(); dialog->raise(); dialog->activateWindow(); }
    void finish() {
        busy = false; timeout.stop(); ui.progress->hide();
        ui.check->setEnabled(available); ui.download->setEnabled(release.build > TSS_UPDATE_BUILD && !ready);
        ui.install->setEnabled(ready);
        lock.reset();
    }
    void fail(const QString& message) {
        ready = false;
        ui.status->setText(UpdateController::tr("Update failed: %1").arg(message));
        finish();
        if (manual) show();
    }
    bool begin() {
        lock = std::make_unique<QLockFile>(original + ".tss-update.lock");
        lock->setStaleLockTime(0);
        if (!lock->tryLock()) {
            fail(UpdateController::tr("The other application is already checking or downloading an update."));
            return false;
        }
        busy = true;
        ui.check->setEnabled(false); ui.download->setEnabled(false); ui.install->setEnabled(false);
        return true;
    }
    void check(bool requested) {
        if (busy || !available) return;
        manual = requested;
        if (!begin()) return;
        ready = false; release = {}; mirror = 0;
        ui.status->setText(UpdateController::tr("Checking for updates..."));
        fetch();
    }
    void fetch() {
        QNetworkRequest request(QUrl(mirrors.at(mirror) + "latest.tssupdate"));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
        request.setTransferTimeout(30000);
        reply = network.get(request);
        auto r = reply.data();
        auto bytes = std::make_shared<QByteArray>();
        QObject::connect(r, &QIODevice::readyRead, owner, [r, bytes] {
            bytes->append(r->readAll());
            if (bytes->size() > 32768) r->abort();
        });
        QObject::connect(r, &QNetworkReply::finished, owner, [this, r, bytes] {
            bytes->append(r->readAll()); r->deleteLater(); reply = nullptr;
            try {
                if (r->error() != QNetworkReply::NoError || r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
                    throw std::runtime_error("Update server is unavailable");
                release = tss::appimage::verify(*bytes, TSS_UPDATE_RSA_PEM, TSS_UPDATE_APP,
                    TSS_UPDATE_VERSION, TSS_UPDATE_BUILD, QDateTime::currentSecsSinceEpoch());
                settings.setValue("lastCheck", QDateTime::currentSecsSinceEpoch());
                if (release.build == TSS_UPDATE_BUILD) {
                    ui.status->setText(UpdateController::tr("You have the latest version."));
                    finish(); return;
                }
                ui.status->setText(UpdateController::tr("Available: %1 %2").arg(TSS_UPDATE_APP, release.version));
                finish();
                if (ui.automaticDownload->isChecked()) download();
                else if (manual || settings.value("notifiedBuild").toLongLong() != release.build) {
                    settings.setValue("notifiedBuild", release.build); show();
                }
            } catch (const std::exception& e) {
                if (++mirror < mirrors.size()) fetch(); else fail(QString::fromUtf8(e.what()));
            }
        });
    }
    void download() {
        if (busy || release.build <= TSS_UPDATE_BUILD || !begin()) return;
        ready = false;
        const auto cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QDir().mkpath(cache);
        staging = std::make_shared<QTemporaryDir>(cache + "/update-XXXXXX");
        if (!staging->isValid()) { fail(staging->errorString()); return; }
        ui.progress->show(); ui.progress->setRange(0, 0);
        ui.status->setText(UpdateController::tr("Preparing download..."));
        const auto dir = staging; const auto source = original;
        auto watcher = new QFutureWatcher<bool>(owner);
        QObject::connect(watcher, &QFutureWatcher<bool>::finished, owner, [this, watcher] {
            const bool copied = watcher->result(); watcher->deleteLater();
            if (!copied) { fail("Cannot create a temporary AppImage copy"); return; }
            process.setProcessEnvironment(cleanEnvironment());
            process.setWorkingDirectory(staging->path());
            ui.status->setText(UpdateController::tr("Downloading update (%1 of %2)...").arg(1).arg(1));
            process.start(helper, {"--appimage-extract-and-run", "--overwrite", "--update-info",
                "zsync|" + mirrors.at(mirror) + release.zsync, candidate()});
            timeout.start(30 * 60 * 1000);
        });
        watcher->setFuture(QtConcurrent::run([dir, source] { return QFile::copy(source, dir->filePath("candidate.AppImage")); }));
    }
    void downloaded(int code, QProcess::ExitStatus status) {
        timeout.stop();
        if (!busy) return;
        if (code || status != QProcess::NormalExit) {
            fail(UpdateController::tr("The download was interrupted. You can retry later.")); return;
        }
        const auto dir = staging; const auto expected = release;
        auto watcher = new QFutureWatcher<bool>(owner);
        QObject::connect(watcher, &QFutureWatcher<bool>::finished, owner, [this, watcher] {
            const bool verified = watcher->result(); watcher->deleteLater();
            if (!verified) { fail("Downloaded AppImage failed SHA-256 verification"); return; }
            ready = true;
            ui.status->setText(UpdateController::tr("The update is ready. Restart to install it."));
            finish();
            if (manual || settings.value("readyBuild").toLongLong() != release.build) {
                settings.setValue("readyBuild", release.build); show();
            }
        });
        watcher->setFuture(QtConcurrent::run([dir, expected] {
            return tss::appimage::matches(dir->filePath("candidate.AppImage"), expected);
        }));
    }
    void install() {
        if (busy || !ready) return;
        if (!canInstall()) {
            QMessageBox::information(dialog, UpdateController::tr("Application updates"),
                UpdateController::tr("Finish device operations and disconnect devices before updating the applications."));
            return;
        }
        if (!begin()) return;
        QFileInfo current(original);
        if (current.isSymLink() || current.size() != originalInfo.size()
            || current.lastModified() != originalInfo.lastModified()
            || release.expires <= QDateTime::currentSecsSinceEpoch()
            || !tss::appimage::matches(candidate(), release)) {
            fail("The installed or downloaded AppImage has changed. Check for updates again."); return;
        }
        // Резервная копия остаётся рядом с приложением. QSaveFile делает атомарную замену
        // в том же каталоге, даже если временная загрузка находится на другом диске.
        QString backup;
        try { backup = tss::appimage::install(original, candidate(), release); }
        catch (const std::exception& e) { fail(QString::fromUtf8(e.what())); return; }
        ready = false; finish();
        // Qt обрабатывает обычное закрытие окон. Отказ от закрытия не теряет данные.
        const bool quitOnClose = QApplication::quitOnLastWindowClosed();
        QApplication::setQuitOnLastWindowClosed(false);
        dialog->hide();
        if (!window->close()) { QApplication::setQuitOnLastWindowClosed(quitOnClose); return; }
        QProcess restart;
        restart.setProgram(original);
        restart.setArguments(QCoreApplication::arguments().mid(1));
        restart.setProcessEnvironment(cleanEnvironment());
        if (!restart.startDetached()) {
            window->show(); QApplication::setQuitOnLastWindowClosed(quitOnClose);
            fail("Could not start the new version. Previous AppImage: " + backup); return;
        }
        QCoreApplication::quit();
    }
};

UpdateController::UpdateController(QWidget* window, QAction* action, std::function<bool()> canInstall)
    : QObject(window), d(std::make_unique<Impl>(this, window, std::move(canInstall))) {
    d->ui.setupUi(d->dialog); d->dialog->installEventFilter(this); d->ui.notes->hide();
    d->settings.beginGroup("AppImageUpdate");
    d->original = qEnvironmentVariable("APPIMAGE");
    d->originalInfo = QFileInfo(d->original);
    d->helper = QCoreApplication::applicationDirPath() + "/../libexec/appimageupdatetool.AppImage";
    d->available = d->originalInfo.isAbsolute() && d->originalInfo.isFile() && !d->originalInfo.isSymLink()
        && d->originalInfo.isWritable() && QFileInfo(d->originalInfo.absolutePath()).isWritable()
        && QFileInfo::exists(d->helper);
    d->mirrors = {QString("https://tsslimiter.com/updates/linux/%1/").arg(TSS_UPDATE_CHANNEL),
                  QString("https://tsslimiter.ru/updates/linux/%1/").arg(TSS_UPDATE_CHANNEL)};
    d->ui.automaticCheck->setChecked(d->settings.value("automaticCheck", true).toBool());
    d->ui.automaticDownload->setChecked(d->settings.value("automaticDownload", true).toBool());
    connect(action, &QAction::triggered, this, [this] { d->show(); });
    connect(d->ui.later, &QPushButton::clicked, d->dialog, &QDialog::hide);
    connect(d->ui.check, &QPushButton::clicked, this, [this] { d->check(true); });
    connect(d->ui.download, &QPushButton::clicked, this, [this] { d->download(); });
    connect(d->ui.install, &QPushButton::clicked, this, [this] { d->install(); });
    connect(d->ui.automaticCheck, &QCheckBox::toggled, this, [this](bool value) { d->settings.setValue("automaticCheck", value); });
    connect(d->ui.automaticDownload, &QCheckBox::toggled, this, [this](bool value) { d->settings.setValue("automaticDownload", value); });
    connect(&d->process, &QProcess::readyReadStandardOutput, this, [this] { d->process.readAllStandardOutput(); });
    connect(&d->process, &QProcess::readyReadStandardError, this, [this] { d->process.readAllStandardError(); });
    connect(&d->process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) { d->downloaded(code, status); });
    connect(&d->process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) d->fail(d->process.errorString());
    });
    d->timeout.setSingleShot(true);
    connect(&d->timeout, &QTimer::timeout, this, [this] { d->process.kill(); });
    const auto check = [this] {
        const auto last = d->settings.value("lastCheck").toLongLong(), now = QDateTime::currentSecsSinceEpoch();
        if (d->ui.automaticCheck->isChecked() && (now - last >= 86400 || now < last)) d->check(false);
    };
    QTimer::singleShot(10000, this, check);
    auto timer = new QTimer(this); connect(timer, &QTimer::timeout, this, check); timer->start(3600000);
    d->finish();
    if (!d->available) d->ui.status->setText(tr("Automatic updates require a writable AppImage. Move it to a folder you own and start it there."));
}
UpdateController::~UpdateController() {
    if (d->reply) { d->reply->disconnect(this); d->reply->abort(); }
    d->process.disconnect(this); d->process.kill(); d->process.waitForFinished(1000);
}
bool UpdateController::eventFilter(QObject* object, QEvent* event) {
    if (object == d->dialog && event->type() == QEvent::LanguageChange) d->ui.retranslateUi(d->dialog);
    return QObject::eventFilter(object, event);
}
