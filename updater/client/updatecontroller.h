// SPDX-License-Identifier: MIT
#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QPointer>
#include <QSaveFile>
#include <QLockFile>
#include <functional>
#include <memory>
#include "updatecore.h"

class QAction;
class QDialog;
class QNetworkReply;
namespace Ui { class TSSUpdateDialog; }

class UpdateController final : public QObject {
    Q_OBJECT
public:
    UpdateController(QWidget* window, QAction* action, std::function<bool()> canInstall);
    ~UpdateController() override;
    // Вызывается после создания QApplication, до создания главного окна.
    static bool acquireInstallationLock();
private:
    void show();
    void check(bool manual);
    void requestManifest();
    void download();
    void nextFile();
    void install();
    void fail(const QString& error);
    void setStatus(const QString& message);
    void finish();
    bool eventFilter(QObject* object, QEvent* event) override;
    QWidget* m_window;
    QDialog* m_dialog;
    std::unique_ptr<Ui::TSSUpdateDialog> m_ui;
    QNetworkAccessManager m_network;
    QSettings m_settings;
    std::function<bool()> m_canInstall;
    QString m_root, m_cache, m_envelope;
    QStringList m_mirrors;
    std::unique_ptr<QLockFile> m_lock;
    std::unique_ptr<QSaveFile> m_output;
    QPointer<QNetworkReply> m_reply;
    tss::update::Manifest m_installed, m_release;
    std::vector<tss::update::File> m_needed;
    size_t m_index = 0;
    int m_mirror = 0;
    qint64 m_received = 0, m_completed = 0, m_total = 0;
    bool m_manual = false, m_busy = false, m_ready = false, m_available = false;
};
