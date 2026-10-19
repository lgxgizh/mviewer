// MainWindow update-check and crash-report notification responsibilities.
#include "mainwindow_p.h"

#include "runtime_storage.h"

#include <QtConcurrent/QtConcurrent>
#include <QThreadPool>

void MainWindow::checkForUpdates(bool silent)
{
    if (m_updateChecking)
        return;
    m_updateChecking = true;
    if (!silent)
        statusBar()->showMessage(tr("Checking for updates..."), 2000);

    // M24: bounded pool (max 1 thread) instead of a detached std::thread.
    // checkGitHub() performs a synchronous network request (WinHttp timeouts
    // bound it to ~35 s worst case); the result is marshaled back via qApp and
    // guarded with QPointer so a window destroyed mid-request never gets a
    // dangling call. The pool waits at app exit, so no worker survives teardown.
    static QThreadPool s_updatePool;
    s_updatePool.setMaxThreadCount(1);
    QPointer<MainWindow> self(this);
    auto updateFuture = QtConcurrent::run(
        &s_updatePool,
        [self, silent]()
        {
            mviewer::core::UpdateChecker checker(MVIEWER_VERSION_STRING);
            checker.checkGitHub("lgxgizh/mviewer",
                                [self, silent](const mviewer::core::UpdateInfo &info)
                                {
                                    QMetaObject::invokeMethod(qApp,
                                                              [self, info, silent]()
                                                              {
                                                                  if (!self)
                                                                      return;
                                                                  self->m_updateChecking = false;
                                                                  self->onUpdateChecked(info,
                                                                                        silent);
                                                              });
                                });
        });
    Q_UNUSED(updateFuture);
}

void MainWindow::onUpdateChecked(const mviewer::core::UpdateInfo &info, bool silent)
{
    if (!isVisible() || sessionPromptsSuppressed())
        return;
    if (!info.error.empty())
    {
        if (!silent)
            QMessageBox::warning(this, tr("Update check failed"),
                                 tr("Could not retrieve update information:\n%1")
                                     .arg(QString::fromStdString(info.error)));
        return;
    }
    if (info.hasUpdate)
    {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Information);
        box.setWindowTitle(tr("New version available"));
        box.setText(tr("New version %1 is available (current %2).")
                        .arg(QString::fromStdString(info.latestVersion),
                             QString::fromStdString(info.currentVersion)));
        box.setInformativeText(tr("Update recommended for the latest features and bug fixes."));
        QPushButton *openBtn = box.addButton(tr("Go to Download Page"), QMessageBox::AcceptRole);
        box.addButton(tr("Later"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == openBtn)
            QDesktopServices::openUrl(QUrl(QString::fromStdString(info.releaseUrl)));
    }
    else if (!silent)
    {
        QMessageBox::information(this, tr("Up to Date"),
                                 tr("You are on the latest version (%1).")
                                     .arg(QString::fromStdString(info.currentVersion)));
    }
}

void MainWindow::maybeShowCrashReport()
{
    if (!isVisible() || sessionPromptsSuppressed())
        return;
    const QString base = mviewer::runtime::writableDirectory(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return;
    const QString dir = QDir(base).filePath(QStringLiteral("crash-reports"));
    QDir d(dir);
    if (!d.exists())
        return;
    const QFileInfoList dumps = d.entryInfoList(QStringList() << "*.dmp", QDir::Files, QDir::Time);
    if (dumps.isEmpty())
        return;
    const QFileInfo &newest = dumps.first();

    // Only prompt once per crash dump (track last-seen mtime in QSettings).
    QSettings settings;
    const qint64 lastCheck = settings.value("crashReportLastCheck", 0).toLongLong();
    const qint64 mtime = newest.lastModified().toSecsSinceEpoch();
    if (mtime <= lastCheck)
        return;
    settings.setValue("crashReportLastCheck", QDateTime::currentSecsSinceEpoch());

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Crash Report"));
    box.setText(tr("An application crash was detected (%1).\nThe crash dump was saved to:\n%2")
                    .arg(newest.lastModified().toString(), newest.absoluteFilePath()));
    box.setInformativeText(tr("You can send this file with a problem description to the developers "
                              "to help locate and fix the issue."));
    QPushButton *openBtn = box.addButton(tr("Open Crash Folder"), QMessageBox::ActionRole);
    box.addButton(tr("Ignore"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == openBtn)
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}
