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

@interface TSSSparkleUserDriver : SPUStandardUserDriver
@end

@implementation TSSSparkleUserDriver
- (void)showUpdaterError:(NSError *)error acknowledgement:(void (^)(void))acknowledgement {
    // Меняем только пояснение App Translocation, сохраняя обработку ошибки в Sparkle.
    if ([error.domain isEqualToString:SUSparkleErrorDomain] && error.code == SURunningTranslocated) {
        const auto title = UpdateController::tr("Cannot update a temporary copy of the application").toUtf8();
        const auto text = UpdateController::tr(
            "macOS has opened a temporary, read-only copy of this application (App Translocation), "
            "so it cannot be updated in this session.\n\n"
            "Quit the application. In Finder, move the original .app to Applications (/Applications) "
            "or to your personal Applications folder (~/Applications), then launch it from there "
            "and check for updates again.\n\n"
            "If the .app is already there, move it to another folder and back using Finder before relaunching.").toUtf8();
        NSMutableDictionary* userInfo = [error.userInfo mutableCopy];
        userInfo[NSLocalizedDescriptionKey] = [NSString stringWithUTF8String:title.constData()];
        userInfo[NSLocalizedRecoverySuggestionErrorKey] = [NSString stringWithUTF8String:text.constData()];
        error = [NSError errorWithDomain:error.domain code:error.code userInfo:userInfo];
    }
    [super showUpdaterError:error acknowledgement:acknowledgement];
}
@end

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
    TSSSparkleUserDriver* userDriver;
    SPUUpdater* sparkle;
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
    NSBundle* bundle = [NSBundle mainBundle];
    d->userDriver = [[TSSSparkleUserDriver alloc] initWithHostBundle:bundle delegate:nil];
    d->sparkle = [[SPUUpdater alloc] initWithHostBundle:bundle applicationBundle:bundle
        userDriver:d->userDriver delegate:d->delegate];
    SPUUpdater* updater = d->sparkle;
    d->ui.automaticCheck->setChecked(updater.automaticallyChecksForUpdates);
    d->ui.automaticDownload->setChecked(updater.automaticallyDownloadsUpdates);
    connect(d->ui.automaticCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        d->sparkle.automaticallyChecksForUpdates = enabled;
    });
    connect(d->ui.automaticDownload, &QCheckBox::toggled, this, [this](bool enabled) {
        d->sparkle.automaticallyDownloadsUpdates = enabled;
    });
    connect(action, &QAction::triggered, this, [this] {
        d->ui.automaticCheck->setChecked(d->sparkle.automaticallyChecksForUpdates);
        d->ui.automaticDownload->setChecked(d->sparkle.automaticallyDownloadsUpdates);
        d->dialog->show(); d->dialog->raise(); d->dialog->activateWindow();
    });
    connect(d->ui.later, &QPushButton::clicked, d->dialog, &QDialog::hide);
    connect(d->ui.check, &QPushButton::clicked, this, [this] {
        d->dialog->hide();
        [d->sparkle checkForUpdates];
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
    NSError* startupError = nil;
    if (![d->sparkle startUpdater:&startupError]) {
        // Как и стандартный контроллер Sparkle, показываем ошибку после запуска приложения.
        QTimer::singleShot(1000, this, [this, startupError] {
            [d->userDriver showUpdaterError:startupError acknowledgement:^{}];
        });
    }
}
UpdateController::~UpdateController() { d->delegate.pendingInstall = nil; }
bool UpdateController::eventFilter(QObject* object, QEvent* event) {
    if (object == d->dialog && event->type() == QEvent::LanguageChange) d->ui.retranslateUi(d->dialog);
    return QObject::eventFilter(object, event);
}
