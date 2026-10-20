// SPDX-License-Identifier: MIT
#include "updatecontroller.h"
#include "buildconfig.h"
#include "ui_updatedialog.h"
#include <QAction>
#include <QDialog>
#include <QEvent>
#include <QLocale>
#include <QTimer>
#import <Cocoa/Cocoa.h>
#import <Sparkle/Sparkle.h>

@interface TSSSparkleDelegate : NSObject <SPUUpdaterDelegate> {
@public
    std::function<bool()> canInstall;
    BOOL russianMirror;
}
@property(copy) void (^pendingInstall)(void);
@end

@implementation TSSSparkleDelegate
- (NSString *)feedURLStringForUpdater:(SPUUpdater *)updater {
    return [NSString stringWithFormat:@"https://tsslimiter.%@/updates/macos/%s/appcast.xml",
            russianMirror ? @"ru" : @"com", TSS_UPDATE_CHANNEL];
}
- (BOOL)updater:(SPUUpdater *)updater mayPerformUpdateCheck:(SPUUpdateCheck)check
         error:(NSError *__autoreleasing *)error {
    if (canInstall()) return YES;
    if (error) {
        const auto text = UpdateController::tr("Finish device operations and disconnect devices before updating the applications.").toUtf8();
        *error = [NSError errorWithDomain:@"TSSUpdater" code:1
            userInfo:@{NSLocalizedDescriptionKey: [NSString stringWithUTF8String:text.constData()]}];
    }
    return NO;
}
- (BOOL)updater:(SPUUpdater *)updater shouldPostponeRelaunchForUpdate:(SUAppcastItem *)item
        untilInvokingBlock:(void (^)(void))handler {
    if (canInstall()) return NO;
    self.pendingInstall = handler;
    return YES;
}
- (void)updater:(SPUUpdater *)updater didAbortWithError:(NSError *)error {
    // При недоступном зеркале следующая проверка использует другое.
    NSError* underlying = error;
    for (int depth = 0; underlying && depth < 8; ++depth) {
        if ([underlying.domain isEqualToString:NSURLErrorDomain]) {
            russianMirror = !russianMirror;
            break;
        }
        underlying = underlying.userInfo[NSUnderlyingErrorKey];
    }
}
@end

struct UpdateController::Impl {
    QDialog* dialog;
    Ui::TSSUpdateDialog ui;
    TSSSparkleDelegate* delegate;
    SPUStandardUpdaterController* sparkle;
    explicit Impl(QWidget* window) : dialog(new QDialog(window)) {}
};

UpdateController::UpdateController(QWidget* window, QAction* action, std::function<bool()> canInstall)
    : QObject(window), d(std::make_unique<Impl>(window)) {
    d->ui.setupUi(d->dialog);
    d->dialog->installEventFilter(this);
    d->ui.notes->hide(); d->ui.progress->hide();
    d->ui.download->hide(); d->ui.install->hide();
    d->delegate = [TSSSparkleDelegate new];
    d->delegate->canInstall = std::move(canInstall);
    d->delegate->russianMirror = QLocale().language() == QLocale::Russian;
    d->sparkle = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO
        updaterDelegate:d->delegate userDriverDelegate:nil];
    SPUUpdater* updater = d->sparkle.updater;
    d->ui.automaticCheck->setChecked(updater.automaticallyChecksForUpdates);
    d->ui.automaticDownload->setChecked(updater.automaticallyDownloadsUpdates);
    connect(d->ui.automaticCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        d->sparkle.updater.automaticallyChecksForUpdates = enabled;
    });
    connect(d->ui.automaticDownload, &QCheckBox::toggled, this, [this](bool enabled) {
        d->sparkle.updater.automaticallyDownloadsUpdates = enabled;
    });
    connect(action, &QAction::triggered, this, [this] {
        d->ui.automaticCheck->setChecked(d->sparkle.updater.automaticallyChecksForUpdates);
        d->ui.automaticDownload->setChecked(d->sparkle.updater.automaticallyDownloadsUpdates);
        d->dialog->show(); d->dialog->raise(); d->dialog->activateWindow();
    });
    connect(d->ui.later, &QPushButton::clicked, d->dialog, &QDialog::hide);
    connect(d->ui.check, &QPushButton::clicked, this, [this] {
        d->dialog->hide();
        [d->sparkle checkForUpdates:nil];
    });
    auto timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        if (d->delegate.pendingInstall && d->delegate->canInstall()) {
            auto handler = d->delegate.pendingInstall;
            d->delegate.pendingInstall = nil;
            handler();
        }
    });
    timer->start(1000);
    [d->sparkle startUpdater];
}
UpdateController::~UpdateController() { d->delegate.pendingInstall = nil; }
bool UpdateController::eventFilter(QObject* object, QEvent* event) {
    if (object == d->dialog && event->type() == QEvent::LanguageChange) d->ui.retranslateUi(d->dialog);
    return QObject::eventFilter(object, event);
}
