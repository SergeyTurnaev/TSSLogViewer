// SPDX-License-Identifier: MIT
#include "updatecontroller.h"
#include "ui_updatedialog.h"
#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QNetworkReply>
#include <QProcess>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <shellapi.h>

using namespace tss::update;
namespace {
QString text(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }
fs::path path(const QString& s) { return fs::path(s.toStdWString()); }
QString rootId(const QString& root) { return QString::fromLatin1(QCryptographicHash::hash(root.toLower().toUtf8(), QCryptographicHash::Sha256).toHex().left(24)); }
QNetworkRequest request(const QUrl& url) {
    QNetworkRequest result(url);
    result.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    result.setTransferTimeout(30000);
    result.setRawHeader("User-Agent", "TSSUpdater/1");
    return result;
}
Handle installationLock;
}

bool UpdateController::acquireInstallationLock() {
    const auto root = path(QCoreApplication::applicationDirPath());
    if (!fs::exists(root / UseLock)) return true;
    if (fs::exists(root / L".tss-update" / L"journal")) {
        QMessageBox::warning(nullptr, tr("Application updates"), tr("An interrupted update needs recovery. Start the application using its shortcut or run TSSUpdater.exe."));
        return false;
    }
    installationLock.value = CreateFileW((root / UseLock).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!installationLock.valid()) {
        QMessageBox::information(nullptr, tr("Application updates"), tr("An update is in progress. Please try again shortly."));
        return false;
    }
    return true;
}

UpdateController::UpdateController(QWidget* window, QAction* action, std::function<bool()> canInstall)
    : QObject(window), m_window(window), m_dialog(new QDialog(window)), m_ui(std::make_unique<Ui::TSSUpdateDialog>()),
      m_settings(QStringLiteral("TSSRacing"), QStringLiteral("TSSUpdater")), m_canInstall(std::move(canInstall)) {
    m_ui->setupUi(m_dialog);
    m_dialog->installEventFilter(this);
    m_root = QCoreApplication::applicationDirPath();
    m_cache = QDir(qEnvironmentVariable("LOCALAPPDATA")).filePath("TSSRacing/TSSUpdater/" + rootId(m_root));
    QDir().mkpath(m_cache + "/blobs");
    m_settings.beginGroup(rootId(m_root));
    m_ui->automaticCheck->setChecked(m_settings.value("automaticCheck", true).toBool());
    m_ui->automaticDownload->setChecked(m_settings.value("automaticDownload", true).toBool());
    m_ui->progress->hide(); m_ui->install->setEnabled(false); m_ui->download->setEnabled(false);
    connect(action, &QAction::triggered, this, &UpdateController::show);
    connect(m_ui->check, &QPushButton::clicked, this, [this] { check(true); });
    connect(m_ui->download, &QPushButton::clicked, this, &UpdateController::download);
    connect(m_ui->install, &QPushButton::clicked, this, &UpdateController::install);
    connect(m_ui->later, &QPushButton::clicked, m_dialog, &QDialog::hide);
    connect(m_ui->automaticCheck, &QCheckBox::toggled, this, [this](bool value) { m_settings.setValue("automaticCheck", value); });
    connect(m_ui->automaticDownload, &QCheckBox::toggled, this, [this](bool value) { m_settings.setValue("automaticDownload", value); });
    try {
        m_installed = verifyManifest(readFile(path(m_root) / InstalledManifest), false);
        const auto channel = m_installed.product == "tss-suite-windows-x64" ? "suite" : "viewer";
        m_mirrors = {QString("https://tsslimiter.com/updates/windows/%1/").arg(channel), QString("https://tsslimiter.ru/updates/windows/%1/").arg(channel)};
        m_available = true;
    } catch (...) {
        setStatus(tr("Automatic updates are not configured for this build. Install a signed release to enable them."));
        m_ui->check->setEnabled(false);
    }
    const auto autoCheck = [this] {
        const auto last = m_settings.value("lastCheck").toLongLong();
        const auto now = QDateTime::currentSecsSinceEpoch();
        if (m_available && m_ui->automaticCheck->isChecked() && (now - last >= 86400 || now < last)) check(false);
    };
    QTimer::singleShot(10000, this, autoCheck);
    auto timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, autoCheck);
    timer->start(3600000);
    const auto args = QCoreApplication::arguments();
    const int resultIndex = args.indexOf("--tss-update-result");
    if (resultIndex >= 0 && resultIndex + 1 < args.size()) {
        const auto token = args[resultIndex + 1];
        if (token.size() == 32 && std::all_of(token.begin(), token.end(), [](QChar c) { return QStringLiteral("0123456789abcdef").contains(c); })) {
            QTimer::singleShot(0, this, [this, token] {
                try {
                    const auto result = readFile(path(m_root + "/.tss-update/result-" + token + ".txt"));
                    if (result == "OK") return;
                    setStatus(tr("The update could not be installed. Your previous version was preserved.\n%1").arg(text(result)));
                } catch (...) { setStatus(tr("The update was interrupted. The previous installation has been restored.")); }
                show();
            });
        }
    }
}
UpdateController::~UpdateController() { if (m_reply) { m_reply->disconnect(this); m_reply->abort(); } }
bool UpdateController::eventFilter(QObject* object, QEvent* event) {
    if (object == m_dialog && event->type() == QEvent::LanguageChange) m_ui->retranslateUi(m_dialog);
    return QObject::eventFilter(object, event);
}
void UpdateController::setStatus(const QString& message) { m_ui->status->setText(message); }
void UpdateController::show() { m_dialog->show(); m_dialog->raise(); m_dialog->activateWindow(); }
void UpdateController::finish() {
    m_busy = false; m_reply = nullptr; m_output.reset(); m_lock.reset();
    m_ui->check->setEnabled(m_available); m_ui->download->setEnabled(!m_release.files.empty() && !m_ready); m_ui->install->setEnabled(m_ready);
}
void UpdateController::fail(const QString& error) {
    m_ready = false; setStatus(tr("Update failed: %1").arg(error)); m_ui->progress->hide(); finish();
    // Фоновые сетевые ошибки не мешают работе и не открывают диалоги.
    if (m_manual) show();
}
void UpdateController::check(bool manual) {
    if (!m_available || m_busy) return;
    m_manual = manual;
    m_lock = std::make_unique<QLockFile>(m_cache + "/operation.lock");
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock()) { fail(tr("The other application is already checking or downloading an update.")); return; }
    m_busy = true; m_ready = false; m_release = {}; m_mirror = 0;
    m_ui->check->setEnabled(false); m_ui->download->setEnabled(false); m_ui->install->setEnabled(false);
    setStatus(tr("Checking for updates...")); requestManifest();
}
void UpdateController::requestManifest() {
    auto reply = m_network.get(request(QUrl(m_mirrors.at(m_mirror) + "latest.tssupdate"))); m_reply = reply;
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [reply, bytes] {
        bytes->append(reply->readAll()); if (bytes->size() > static_cast<qsizetype>(MaxManifestSize)) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes] {
        bytes->append(reply->readAll()); reply->deleteLater();
        try {
            if (reply->error() != QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) throw std::runtime_error("Update server is unavailable");
            auto release = verifyManifest(bytes->toStdString());
            if (release.product != m_installed.product || compareVersion(release.version, m_installed.version) < 0 || compareVersion(release.viewerVersion, m_installed.viewerVersion) < 0) throw std::runtime_error("Release does not match the installed product");
            m_settings.setValue("lastCheck", QDateTime::currentSecsSinceEpoch());
            if (release.sequence <= m_installed.sequence) { setStatus(tr("You have the latest version.")); finish(); return; }
            m_release = std::move(release); m_envelope = QString::fromUtf8(*bytes);
            m_ui->notes->setPlainText(text(m_release.notes));
            setStatus(tr("Available: TSSLimiterX %1, TSSLogViewer %2").arg(text(m_release.version), text(m_release.viewerVersion)));
            if (m_release.product == "tss-viewer-windows-x64") setStatus(tr("Available: TSSLogViewer %1").arg(text(m_release.viewerVersion)));
            finish();
            if (m_ui->automaticDownload->isChecked()) download();
            else if (m_manual || m_settings.value("notifiedSequence").toULongLong() != m_release.sequence) { m_settings.setValue("notifiedSequence", static_cast<qulonglong>(m_release.sequence)); show(); }
        } catch (const std::exception& error) {
            if (++m_mirror < m_mirrors.size()) requestManifest(); else fail(text(error.what()));
        }
    });
}
void UpdateController::download() {
    if (m_busy || m_release.files.empty()) return;
    m_lock = std::make_unique<QLockFile>(m_cache + "/operation.lock"); m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock()) { fail(tr("The other application is already checking or downloading an update.")); return; }
    m_busy = true; m_ready = false; m_index = 0; m_completed = 0; m_total = 0;
    m_ui->check->setEnabled(false); m_ui->download->setEnabled(false); m_ui->install->setEnabled(false);
    setStatus(tr("Preparing download...")); m_ui->progress->show(); m_ui->progress->setRange(0, 0);
    const auto release = m_release; const auto root = path(m_root), cache = path(m_cache);
    using Plan = std::pair<std::vector<File>, QString>;
    auto watcher = new QFutureWatcher<Plan>(this);
    connect(watcher, &QFutureWatcher<Plan>::finished, this, [this, watcher] {
        const auto plan = watcher->result(); watcher->deleteLater();
        if (!plan.second.isEmpty()) { fail(plan.second); return; }
        m_needed = plan.first;
        for (const auto& file : m_needed) m_total += file.size;
        try { writeAtomic(path(m_cache + "/release.manifest"), m_envelope.toUtf8().toStdString()); }
        catch (const std::exception& e) { fail(text(e.what())); return; }
        m_ui->progress->setRange(0, 100); nextFile();
    });
    watcher->setFuture(QtConcurrent::run([release, root, cache]() -> Plan {
        try {
            std::vector<File> needed;
            for (const auto& file : release.files) {
                if (file.path == "Prerequisites/vc_redist.x64.exe" && compareVersion(installedRuntime(), release.runtime) >= 0) continue;
                if (matches(root / file.path, file) || matches(cache / L"blobs" / (file.sha256 + ".bin"), file)) continue;
                needed.push_back(file);
            }
            return {needed, {}};
        } catch (const std::exception& error) { return {{}, text(error.what())}; }
    }));
}
void UpdateController::nextFile() {
    if (m_index == m_needed.size()) {
        m_ready = true; setStatus(tr("The update is ready. Restart to install it.")); m_ui->progress->setValue(100); finish();
        if (m_manual || m_settings.value("readySequence").toULongLong() != m_release.sequence) { m_settings.setValue("readySequence", static_cast<qulonglong>(m_release.sequence)); show(); }
        return;
    }
    const auto file = m_needed[m_index]; m_received = 0;
    m_output = std::make_unique<QSaveFile>(m_cache + "/blobs/" + text(file.sha256) + ".bin");
    if (!m_output->open(QIODevice::WriteOnly)) { fail(m_output->errorString()); return; }
    setStatus(tr("Downloading update (%1 of %2)...").arg(m_index + 1).arg(m_needed.size()));
    auto reply = m_network.get(request(QUrl(m_mirrors.at(m_mirror) + "blobs/" + text(file.sha256) + ".bin"))); m_reply = reply;
    auto drain = [this, reply, file] {
        const auto bytes = reply->readAll(); m_received += bytes.size();
        if (m_received > static_cast<qint64>(file.size) || m_output->write(bytes) != bytes.size()) { reply->abort(); return; }
        m_ui->progress->setValue(m_total ? static_cast<int>((m_completed + m_received) * 100 / m_total) : 0);
    };
    connect(reply, &QIODevice::readyRead, this, drain);
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, drain] {
        drain(); reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 || m_received != static_cast<qint64>(file.size)) {
            m_output.reset(); if (++m_mirror < m_mirrors.size()) nextFile(); else fail(tr("The download was interrupted. You can retry later.")); return;
        }
        if (!m_output->commit()) { fail(m_output->errorString()); return; }
        try { if (!matches(path(m_output->fileName()), file)) throw std::runtime_error("Downloaded file failed SHA-256 verification"); }
        catch (const std::exception& e) { fail(text(e.what())); return; }
        m_output.reset(); m_completed += file.size; ++m_index; nextFile();
    });
}
void UpdateController::install() {
    if (!m_ready || m_busy) return;
    if (!m_canInstall()) { QMessageBox::information(m_dialog, tr("Application updates"), tr("Finish device operations and disconnect devices before updating the applications.")); return; }
    try {
        // Фиксируем именно показанный выпуск; другая программа не может подменить его новой проверкой.
        m_lock = std::make_unique<QLockFile>(m_cache + "/operation.lock");
        m_lock->setStaleLockTime(0);
        if (!m_lock->tryLock()) { fail(tr("The other application is already checking or downloading an update.")); return; }
        const auto envelope = readFile(path(m_cache + "/release.manifest"));
        verifyManifest(envelope);
        if (envelope != m_envelope.toUtf8().toStdString()) throw std::runtime_error("The cached release changed. Check for updates again.");
        const auto helper = std::find_if(m_installed.files.begin(), m_installed.files.end(), [](const File& file) { return file.path == "TSSUpdater.exe"; });
        if (helper == m_installed.files.end() || !matches(path(m_root + "/TSSUpdater.exe"), *helper)) throw std::runtime_error("Installed updater failed verification");
        const QString token = QUuid::createUuid().toString(QUuid::Id128);
        const QString watcher = m_cache + "/watch-" + token + ".exe";
        if (!QFile::copy(m_root + "/TSSUpdater.exe", watcher) || !matches(path(watcher), *helper)) throw std::runtime_error("Cannot prepare restart watcher");
        const auto executable = path(m_root + "/TSSUpdater.exe");
        const auto parameters = L"--apply " + quote(path(m_cache).wstring()) + L" " + token.toStdWString() + L" " + text(hashBytes(envelope)).toStdWString();
        SHELLEXECUTEINFOW info{}; info.cbSize = sizeof(info); info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
        info.hwnd = reinterpret_cast<HWND>(m_dialog->winId()); info.lpFile = executable.c_str(); info.lpParameters = parameters.c_str(); info.nShow = SW_HIDE;
        QFile probe(m_root + "/.write-probe-" + token);
        if (!probe.open(QIODevice::WriteOnly | QIODevice::NewOnly)) info.lpVerb = L"runas";
        else { probe.close(); probe.remove(); }
        if (!ShellExecuteExW(&info)) { fail(tr("Update installation was cancelled or could not be started.")); return; }
        if (info.hProcess) CloseHandle(info.hProcess);
        const QString app = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
        if (!QProcess::startDetached(watcher, {"--watch", m_root, token, app}, m_cache)) throw std::runtime_error("Cannot start restart watcher");
        m_dialog->hide(); m_window->close();
        if (!m_window->isVisible()) QCoreApplication::quit();
    } catch (const std::exception& error) { fail(text(error.what())); }
}
