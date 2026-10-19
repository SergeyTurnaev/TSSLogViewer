
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFileSystemWatcher>
#include <list>
#include <memory>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QTranslator;

class MainWindow : public QMainWindow

{
    Q_OBJECT

public:
    MainWindow(std::list<std::shared_ptr<QTranslator>> translators, QWidget *parent = nullptr);
    ~MainWindow();
    void openLog(QString filename);

private slots:
    void openFile();
    void scanDirectory(QString directory);
    void rescanDirectory(QString directory);
    void processLogFile(QString baseDir, QString filename);
    void selectDirectory();
    void resizeColumnsToContents();
    void logSelected();
    void logOpened();
    void logDirectoryChanged(QString path);
    void closePlotTab(int idx);

protected:
    void changeEvent(QEvent *event) override;

private:
    Ui::MainWindow *ui;
    QFileSystemWatcher m_logWatcher;
    QHash<QString, int> m_files;
    std::list<std::shared_ptr<QTranslator>> m_translators;

    void setupLocale();
};

#endif // MAINWINDOW_H
