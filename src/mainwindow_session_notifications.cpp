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
        statusBar()->showMessage(tr("正在检查更新..."), 2000);

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
    if (!info.error.empty())
    {
        if (!silent)
            QMessageBox::warning(
                this, tr("检查更新失败"),
                tr("无法获取更新信息：\n%1").arg(QString::fromStdString(info.error)));
        return;
    }
    if (info.hasUpdate)
    {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Information);
        box.setWindowTitle(tr("发现新版本"));
        box.setText(tr("发现新版本 %1（当前 %2）。")
                        .arg(QString::fromStdString(info.latestVersion),
                             QString::fromStdString(info.currentVersion)));
        box.setInformativeText(tr("建议更新以获得最新功能与缺陷修复。"));
        QPushButton *openBtn = box.addButton(tr("前往下载页"), QMessageBox::AcceptRole);
        box.addButton(tr("稍后"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == openBtn)
            QDesktopServices::openUrl(QUrl(QString::fromStdString(info.releaseUrl)));
    }
    else if (!silent)
    {
        QMessageBox::information(
            this, tr("已是最新"),
            tr("当前已是最新版本（%1）。").arg(QString::fromStdString(info.currentVersion)));
    }
}

void MainWindow::maybeShowCrashReport()
{
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
    box.setWindowTitle(tr("崩溃报告"));
    box.setText(tr("检测到一次应用崩溃（%1）。\n崩溃转储已保存到：\n%2")
                    .arg(newest.lastModified().toString(), newest.absoluteFilePath()));
    box.setInformativeText(tr("可将此文件连同问题描述发送给开发者，以帮助定位并修复问题。"));
    QPushButton *openBtn = box.addButton(tr("打开崩溃目录"), QMessageBox::ActionRole);
    box.addButton(tr("忽略"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == openBtn)
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}
