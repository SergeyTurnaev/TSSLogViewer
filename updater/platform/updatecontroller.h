// SPDX-License-Identifier: MIT
#pragma once
#include <QObject>
#include <functional>
#include <memory>

class QWidget;
class QAction;
class UpdateController final : public QObject {
    Q_OBJECT
public:
    UpdateController(QWidget* window, QAction* action, std::function<bool()> canInstall);
    ~UpdateController() override;
    static bool acquireInstallationLock() { return true; }
private:
    struct Impl;
    std::unique_ptr<Impl> d;
    bool eventFilter(QObject* object, QEvent* event) override;
};
