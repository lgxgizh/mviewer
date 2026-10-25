// MainWindow update-check and crash-report notification responsibilities.
#include "mainwindow_p.h"

#include "runtime_storage.h"

#include <QThreadPool>
#include <QtConcurrent/QtConcurrent>

void MainWindow::checkForUpdates(bool silent)
{
    using mviewer::core::UpdateCheckStart;
    const auto start = mviewer::core::beginUpdateCheck(m_updateGate, silent);
    if (start == UpdateCheckStart::Ignored)
        return;
    if (start == UpdateCheckStart::ManualPending)
    {
        statusBar()->showMessage(tr("Checking for updates…"), 2000);
        return;
    }
    if (!silent)
        statusBar()->showMessage(tr("Checking for updates…"), 2000);

    // M24: bounded pool (max 1 thread) instead of a detached std::thread.
    // checkGitHub() performs a synchronous network request (WinHttp timeouts
    // bound it to ~35 s worst case); the result is marshaled back via qApp and
    // guarded with QPointer so a window destroyed mid-request never gets a
    // dangling call. The pool waits at app exit, so no worker survives teardown.
    // A manual request that arrives while this one runs is recorded on the gate
    // and does not start a second HTTP request.
    static QThreadPool s_updatePool;
    s_updatePool.setMaxThreadCount(1);
    QPointer<MainWindow> self(this);
    auto updateFuture = QtConcurrent::run(
        &s_updatePool,
        [self]()
        {
            mviewer::core::UpdateChecker checker(MVIEWER_VERSION_STRING);
            checker.checkGitHub("lgxgizh/mviewer",
                                [self](const mviewer::core::UpdateInfo &info)
                                {
                                    QMetaObject::invokeMethod(qApp,
                                                              [self, info]()
                                                              {
                                                                  if (!self)
                                                                      return;
                                                                  self->onUpdateChecked(info);
                                                              });
                                });
        });
    Q_UNUSED(updateFuture);
}

void MainWindow::onUpdateChecked(const mviewer::core::UpdateInfo &info)
{
    const bool asManual = mviewer::core::finishUpdateCheck(m_updateGate);
    if (!isVisible() || sessionPromptsSuppressed())
        return;
    if (!info.error.empty())
    {
        if (asManual)
            QMessageBox::warning(this, tr("Update check failed"),
                                 tr("Could not retrieve update information:\n%1")
                                     .arg(QString::fromStdString(info.error)));
        return;
    }
    if (info.hasUpdate)
    {
        QSettings settings;
        const std::string skipped =
            settings.value(QStringLiteral("update/skippedVersion")).toString().toStdString();
        const auto decision = mviewer::core::decideUpdatePrompt(!asManual, info.latestVersion,
                                                                skipped, updatePromptUiBusy());
        if (decision.kind == mviewer::core::UpdatePromptKind::None)
            return;
        if (decision.kind == mviewer::core::UpdatePromptKind::StatusBarNotice)
        {
            showUpdateNotice(info);
            return;
        }
        showUpdateDialog(info);
        return;
    }
    if (asManual)
        QMessageBox::information(this, tr("Up to Date"),
                                 tr("You are on the latest version (%1).")
                                     .arg(QString::fromStdString(info.currentVersion)));
}

void MainWindow::showUpdateNotice(const mviewer::core::UpdateInfo &info)
{
    m_pendingUpdate = info;
    m_hasPendingUpdate = true;
    if (!m_updateNotice)
        return;
    const QString label =
        tr("New version %1 available").arg(QString::fromStdString(info.latestVersion));
    m_updateNotice->setText(QStringLiteral("<a href=\"update\">%1</a>").arg(label));
    m_updateNotice->show();
}

void MainWindow::showUpdateDialog(const mviewer::core::UpdateInfo &info)
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(tr("New version available"));
    box.setText(tr("New version %1 is available (current %2).")
                    .arg(QString::fromStdString(info.latestVersion),
                         QString::fromStdString(info.currentVersion)));
    box.setInformativeText(tr("Update recommended for the latest features and bug fixes."));
    auto *dontAutoCheck = new QCheckBox(tr("Don't check for updates automatically"), &box);
    dontAutoCheck->setObjectName(QStringLiteral("updateNagDisableAutoCheck"));
    box.setCheckBox(dontAutoCheck);
    QPushButton *openBtn = box.addButton(tr("Go to Download Page"), QMessageBox::AcceptRole);
    QPushButton *skipBtn = box.addButton(tr("Skip this version"), QMessageBox::ActionRole);
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    box.exec();
    if (box.checkBox() && box.checkBox()->isChecked())
    {
        QSettings settings;
        settings.setValue(QStringLiteral("autoCheckUpdates"), false);
        settings.sync();
    }
    if (box.clickedButton() == skipBtn)
    {
        QSettings settings;
        settings.setValue(QStringLiteral("update/skippedVersion"),
                          QString::fromStdString(info.latestVersion));
        settings.sync();
        if (m_updateNotice)
            m_updateNotice->hide();
        m_hasPendingUpdate = false;
        return;
    }
    if (box.clickedButton() == openBtn)
        QDesktopServices::openUrl(QUrl(QString::fromStdString(info.releaseUrl)));
}

bool MainWindow::updatePromptUiBusy() const
{
    if (isFullScreen())
        return true;
    if (m_imageViewer && (m_imageViewer->isVisible() || m_imageViewer->isFullScreen()))
        return true;
    if (m_compareHost && m_compareHost->isVisible())
        return true;
    const auto usmDialogs = findChildren<UsmCompareDialog *>();
    for (const UsmCompareDialog *dialog : usmDialogs)
    {
        if (dialog && dialog->isVisible())
            return true;
    }
    return false;
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
