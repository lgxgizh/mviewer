// MainWindow workspace/project persistence, session autosave and recovery (M20 P0#1).
#include "mainwindow_p.h"

#include "runtime_storage.h"

#include <QtConcurrent/QtConcurrent>
#include <QSaveFile>
#include <QThreadPool>

#include <unordered_map>

namespace
{

QString appConfigFile(const QString &name)
{
    return mviewer::runtime::filePath(QStandardPaths::AppConfigLocation, name);
}

struct PersistenceSnapshot
{
    std::string rootPath;
    std::string outputPath;
    std::vector<std::string> comparedImages;
    std::string compareSessionJson;
    mviewer::domain::Selection roi;
    std::unordered_map<std::string, std::string> analysisByPath;
    std::unordered_map<std::string, std::string> analysisAnalyzerByPath;
    bool project = false;
    std::string projectName;
    std::string createdIso;
    std::vector<std::string> analyzerPipeline;
    std::string currentImagePath;
    int currentFrameIndex = 0;
    bool currentPlaying = false;
};

struct PersistenceResult
{
    bool cancelled = false;
    bool success = false;
    std::string error;
    size_t imageCount = 0;
    size_t folderCount = 0;
};

// M47: async workspace/project restore result. The worker ONLY reads and
// deserializes (no UI state); the UI applies the parsed document atomically on
// the current generation, so a failed or superseded open never touches the
// live session.
struct RestoreFileResult
{
    bool ok = false;
    mviewer::domain::Workspace workspace;
    QString filePath; // original path, for UI messages
    QString error;    // UI-facing failure reason
};

void runRestoreFile(const QString &filePath, bool project, const TaskScheduler::TaskContext &ctx,
                    RestoreFileResult &result)
{
    if (ctx.isCancelled())
        return;
    result.filePath = filePath;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        result.error = QStringLiteral("无法读取文件：%1").arg(filePath);
        return;
    }
    const QByteArray data = f.readAll();
    if (ctx.isCancelled())
        return;
    if (!project)
    {
        auto ws =
            mviewer::core::deserializeWorkspace(std::string(data.constData(), data.size()));
        if (!ws || ws->empty())
        {
            result.error = QStringLiteral("工作区文件无效或为空。");
            return;
        }
        result.workspace = std::move(*ws);
    }
    else
    {
        mviewer::domain::Project proj;
        if (!mviewer::core::deserializeProject(std::string(data.constData(), data.size()),
                                               proj) ||
            proj.workspace.empty())
        {
            result.error = QStringLiteral("项目文件无效或为空。");
            return;
        }
        result.workspace = std::move(proj.workspace);
    }
    result.ok = true;
}

bool cancelled(const TaskScheduler::TaskContext &ctx,
               const std::shared_ptr<std::atomic<bool>> &cancel)
{
    return ctx.isCancelled() || (cancel && cancel->load(std::memory_order_acquire));
}

void captureAnalysis(PersistenceSnapshot &snapshot, AnalyzerModel *model, const QString &qpath)
{
    if (!model || qpath.isEmpty())
        return;
    const std::string path = qpath.toUtf8().toStdString();
    const QString text = model->resultText(qpath);
    if (text.isEmpty())
        return;
    snapshot.analysisByPath[path] = text.toUtf8().toStdString();
    snapshot.analysisAnalyzerByPath[path] = model->resultAnalyzerId(qpath).toUtf8().toStdString();
}

void applyPersistedCompareContext(mviewer::domain::Workspace &ws,
                                   const PersistenceSnapshot &snapshot)
{
    for (const std::string &path : snapshot.comparedImages)
        ws.comparedImages.push_back(path);
    ws.compareSessionJson = snapshot.compareSessionJson;

    for (auto &folder : ws.folders)
    {
        for (auto &image : folder.imageSet.images)
        {
            const auto it = snapshot.analysisByPath.find(image.filePath);
            if (it != snapshot.analysisByPath.end())
                image.analysis = it->second;
            const auto analyzerIt = snapshot.analysisAnalyzerByPath.find(image.filePath);
            if (analyzerIt != snapshot.analysisAnalyzerByPath.end())
                image.analysisAnalyzerId = analyzerIt->second;
            if (std::find(snapshot.comparedImages.begin(), snapshot.comparedImages.end(),
                          image.filePath) != snapshot.comparedImages.end() &&
                !snapshot.roi.isEmpty())
            {
                image.roiX = snapshot.roi.x;
                image.roiY = snapshot.roi.y;
                image.roiW = snapshot.roi.width;
                image.roiH = snapshot.roi.height;
            }
        }
    }
}

void runPersistence(const PersistenceSnapshot &snapshot, const TaskScheduler::TaskContext &ctx,
                    const std::shared_ptr<std::atomic<bool>> &cancel, PersistenceResult &result)
{
    try
    {
        if (cancelled(ctx, cancel))
        {
            result.cancelled = true;
            return;
        }
        mviewer::domain::Workspace ws =
            ImageRepository::instance().loadWorkspace(snapshot.rootPath);
        if (ws.empty())
        {
            result.error = "当前目录没有可保存的图片。";
            return;
        }
        applyPersistedCompareContext(ws, snapshot);
        if (cancelled(ctx, cancel))
        {
            result.cancelled = true;
            return;
        }

        std::string body;
        if (!snapshot.project)
        {
            body = mviewer::core::serializeWorkspace(ws);
        }
        else
        {
            mviewer::domain::Project project;
            project.name = snapshot.projectName;
            project.filePath = snapshot.outputPath;
            project.appVersion = MVIEWER_VERSION_STRING;
            project.createdIso = snapshot.createdIso;
            project.modifiedIso = snapshot.createdIso;
            project.workspace = ws;
            project.datasetRoots = {snapshot.rootPath};
            project.analyzerPipeline = snapshot.analyzerPipeline;
            body = mviewer::core::serializeProject(project);
        }
        if (cancelled(ctx, cancel))
        {
            result.cancelled = true;
            return;
        }
        if (!mviewer::exportjob::writeTextAtomically(snapshot.outputPath, body))
        {
            result.error = "无法原子写入目标文件。";
            return;
        }
        result.imageCount = ws.imageCount();
        result.folderCount = ws.folderCount();
        result.success = true;
    }
    catch (const std::exception &error)
    {
        result.error = error.what();
    }
    catch (...)
    {
        result.error = "后台持久化任务失败。";
    }
}

} // namespace

void MainWindow::cancelBackgroundPersistence()
{
    ++m_persistenceGeneration;
    if (m_persistenceCancel)
        m_persistenceCancel->store(true, std::memory_order_release);
    TaskScheduler::cancel(m_persistenceTask);
    m_persistenceTask.reset();
    m_persistenceCancel.reset();
}

void MainWindow::saveWorkspace()
{
    if (currentDir().isEmpty())
    {
        QMessageBox::information(this, "保存工作区", "请先打开一个图片目录。");
        return;
    }
    const QString filePath = QFileDialog::getSaveFileName(
        this, "保存工作区", currentDir() + "/workspace.mvws", "MViewer 工作区 (*.mvws)");
    if (filePath.isEmpty())
        return;

    cancelBackgroundPersistence();
    PersistenceSnapshot snapshot;
    snapshot.rootPath = currentDir().toUtf8().toStdString();
    snapshot.outputPath = filePath.toUtf8().toStdString();
    snapshot.currentImagePath = currentImagePath().toUtf8().toStdString();
    if (m_imageViewer && m_imageViewer->currentPath() == currentImagePath())
    {
        snapshot.currentFrameIndex = m_imageViewer->frameIndex();
        snapshot.currentPlaying = m_imageViewer->isPlaying();
    }
    if (m_compareView)
    {
        const QStringList compared = m_compareView->comparedImages();
        for (const QString &path : compared)
            snapshot.comparedImages.push_back(path.toUtf8().toStdString());
        snapshot.roi = m_compareView->currentROI();
        if (m_compareView->compareSession().isValid())
            snapshot.compareSessionJson =
                mviewer::core::serializeCompareSession(m_compareView->compareSession());
    }
    for (const std::string &path : snapshot.comparedImages)
        captureAnalysis(snapshot, m_analyzer, QString::fromUtf8(path.c_str()));
    captureAnalysis(snapshot, m_analyzer, currentImagePath());

    // Compare-session JSON is a value snapshot too; it is applied to the
    // workspace after directory enumeration, entirely on the worker.
    snapshot.compareSessionJson = m_compareView && m_compareView->compareSession().isValid()
                                      ? mviewer::core::serializeCompareSession(
                                            m_compareView->compareSession())
                                      : std::string();

    auto state = std::make_shared<PersistenceResult>();
    auto cancel = m_persistenceCancel = std::make_shared<std::atomic<bool>>(false);
    const uint64_t generation = ++m_persistenceGeneration;
    QPointer<MainWindow> guard(this);
    statusBar()->showMessage(QStringLiteral("正在后台保存工作区…"));
    m_persistenceTask = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [snapshot, cancel, state](const TaskScheduler::TaskContext &ctx)
        { runPersistence(snapshot, ctx, cancel, *state); },
        {}, std::chrono::steady_clock::time_point::max(),
        [guard, state, cancel, generation, filePath]()
        {
            if (!qApp)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, state, cancel, generation, filePath]()
                {
                    MainWindow *window = guard.data();
                    if (!window || generation != window->m_persistenceGeneration)
                        return;
                    window->m_persistenceTask.reset();
                    window->m_persistenceCancel.reset();
                    if (state->cancelled)
                        return;
                    if (!state->success)
                    {
                        QMessageBox::critical(window, "保存工作区",
                                              QString::fromUtf8(state->error.c_str()));
                        return;
                    }
                    window->statusBar()->showMessage(
                        QString("工作区已保存: %1 (%2 张图片, %3 个目录)")
                            .arg(QFileInfo(filePath).fileName())
                            .arg(static_cast<int>(state->imageCount))
                            .arg(static_cast<int>(state->folderCount)));
                },
                Qt::QueuedConnection);
        });
    if (!m_persistenceTask)
    {
        m_persistenceCancel.reset();
        QMessageBox::warning(this, "保存工作区", "后台保存任务被调度器拒绝。");
    }
}

void MainWindow::openWorkspace()
{
    const QString filePath =
        QFileDialog::getOpenFileName(this, "打开工作区", QString(), "MViewer 工作区 (*.mvws)");
    if (filePath.isEmpty())
        return;
    openWorkspaceFile(filePath);
}

void MainWindow::restoreWorkspaceState(const mviewer::domain::Workspace &workspace,
                                       const QString &filePath)
{
    const auto &ws = workspace;

    const QString root =
        QString::fromUtf8(ws.rootPath.data(), static_cast<int>(ws.rootPath.size()));
    changeDirectory(root);
    if (m_workspace)
    {
        m_workspace->setRootPath(root);
        QStringList cmp;
        for (const auto &p : ws.comparedImages)
            cmp.append(QString::fromUtf8(p.data(), static_cast<int>(p.size())));
        m_workspace->setComparedImages(cmp);
        m_workspace->setCompareSessionJson(QString::fromStdString(ws.compareSessionJson));
    }

    QStringList comparePaths;
    comparePaths.reserve(static_cast<int>(ws.comparedImages.size()));
    for (const auto &p : ws.comparedImages)
        comparePaths.push_back(QString::fromUtf8(p.data(), static_cast<int>(p.size())));

    m_analyzer->clearAllResults();
    for (const auto &folder : ws.folders)
    {
        for (const auto &img : folder.imageSet.images)
        {
            if (!img.analysis.empty())
                m_analyzer->setResult(
                                      QString::fromUtf8(img.filePath.data(),
                                                        static_cast<int>(img.filePath.size())),
                                      QString::fromStdString(img.analysis),
                                      QString::fromStdString(img.analysisAnalyzerId));
        }
    }

    std::optional<mviewer::domain::CompareSession> restoredSession;
    bool haveSession = false;
    if (!ws.compareSessionJson.empty())
    {
        restoredSession = mviewer::core::deserializeCompareSession(ws.compareSessionJson);
        haveSession = restoredSession.has_value();
    }
    if (!comparePaths.isEmpty())
    openCompare(comparePaths,
                haveSession ? QString::fromStdString(ws.compareSessionJson) : QString());

    std::string restoredPath;
    mviewer::domain::Selection restoredRoi;
    std::string restoredAnalysis;
    for (const auto &folder : ws.folders)
    {
        for (const auto &img : folder.imageSet.images)
        {
            if (restoredPath.empty() && (img.roiW > 0 || img.roiH > 0 || !img.analysis.empty()))
            {
                restoredRoi = {img.roiX, img.roiY, img.roiW, img.roiH};
                restoredAnalysis = img.analysis;
                restoredPath = img.filePath;
            }
        }
    }
    const QString persistedCurrentPath = QString::fromUtf8(
        ws.currentImagePath.data(), static_cast<int>(ws.currentImagePath.size()));
    if (!persistedCurrentPath.isEmpty() && QFile::exists(persistedCurrentPath))
    {
        restoredPath = persistedCurrentPath.toUtf8().toStdString();
        restoredRoi = {};
        restoredAnalysis.clear();
    }
    if (restoredPath.empty() && !comparePaths.isEmpty())
        restoredPath = comparePaths.first().toUtf8().toStdString();
    else if (restoredPath.empty() && ws.imageCount() > 0)
        restoredPath = ws.folders.front().imageSet.images.front().filePath;

    if (!restoredPath.empty())
    {
        const std::string restoredViewerPath = restoredPath;
        const int restoredFrame = std::max(0, ws.currentFrameIndex);
        const bool restoredPlaying = ws.currentPlaying;
        if (m_imageViewer)
        {
            connect(m_imageViewer, &ImageViewer::imageReady, this,
                    [this, restoredViewerPath, restoredFrame, restoredPlaying](const auto &frame)
                    {
                        if (!m_imageViewer || !frame || frame->metadata().filePath != restoredViewerPath)
                            return;
                        m_imageViewer->setFrameIndex(restoredFrame);
                        if (restoredPlaying)
                            m_imageViewer->play();
                        else
                            m_imageViewer->pause();
                    },
                    Qt::SingleShotConnection);
        }
        m_selection->setCurrentImage(
            QString::fromUtf8(restoredPath.data(), static_cast<int>(restoredPath.size())));
        if (m_imageViewer)
        {
            m_imageViewer->setImage(currentImagePath());
            m_previewPanel->setImage(currentImagePath());
        }
        if (!restoredAnalysis.empty())
            m_analysisPanel->setRegionStats(QString::fromStdString(restoredAnalysis));
        if (!restoredRoi.isEmpty() && m_compareView)
            m_compareView->applyROI(restoredRoi);
    }

    statusBar()->showMessage(QString("工作区已打开: %1 (%2 张图片, %3 个目录)")
                                 .arg(QFileInfo(filePath).fileName())
                                 .arg(static_cast<int>(ws.imageCount()))
                                 .arg(static_cast<int>(ws.folderCount())));
}
void MainWindow::saveProject()
{
    if (currentDir().isEmpty())
    {
        QMessageBox::information(this, "保存项目", "请先打开一个图片目录。");
        return;
    }
    const QString filePath = QFileDialog::getSaveFileName(
        this, "保存项目", currentDir() + "/project.mvproj", "MViewer 项目 (*.mvproj)");
    if (filePath.isEmpty())
        return;

    cancelBackgroundPersistence();
    PersistenceSnapshot snapshot;
    snapshot.project = true;
    snapshot.rootPath = currentDir().toUtf8().toStdString();
    snapshot.outputPath = filePath.toUtf8().toStdString();
    snapshot.projectName = QFileInfo(filePath).baseName().toUtf8().toStdString();
    snapshot.createdIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8().toStdString();
    snapshot.currentImagePath = currentImagePath().toUtf8().toStdString();
    if (m_imageViewer && m_imageViewer->currentPath() == currentImagePath())
    {
        snapshot.currentFrameIndex = m_imageViewer->frameIndex();
        snapshot.currentPlaying = m_imageViewer->isPlaying();
    }
    if (m_compareView)
    {
        for (const QString &path : m_compareView->comparedImages())
            snapshot.comparedImages.push_back(path.toUtf8().toStdString());
        snapshot.roi = m_compareView->currentROI();
        if (m_compareView->compareSession().isValid())
            snapshot.compareSessionJson =
                mviewer::core::serializeCompareSession(m_compareView->compareSession());
    }
    for (const std::string &path : snapshot.comparedImages)
        captureAnalysis(snapshot, m_analyzer, QString::fromUtf8(path.c_str()));
    captureAnalysis(snapshot, m_analyzer, currentImagePath());
    const AnalyzerPipeline pipeline;
    for (const auto &id : pipeline.analyzerIds())
        snapshot.analyzerPipeline.push_back(id);

    auto state = std::make_shared<PersistenceResult>();
    auto cancel = m_persistenceCancel = std::make_shared<std::atomic<bool>>(false);
    const uint64_t generation = ++m_persistenceGeneration;
    QPointer<MainWindow> guard(this);
    statusBar()->showMessage(QStringLiteral("正在后台保存项目…"));
    m_persistenceTask = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [snapshot, cancel, state](const TaskScheduler::TaskContext &ctx)
        { runPersistence(snapshot, ctx, cancel, *state); },
        {}, std::chrono::steady_clock::time_point::max(),
        [guard, state, generation, filePath]()
        {
            if (!qApp)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, state, generation, filePath]()
                {
                    MainWindow *window = guard.data();
                    if (!window || generation != window->m_persistenceGeneration)
                        return;
                    window->m_persistenceTask.reset();
                    window->m_persistenceCancel.reset();
                    if (state->cancelled)
                        return;
                    if (!state->success)
                    {
                        QMessageBox::critical(window, "保存项目",
                                              QString::fromUtf8(state->error.c_str()));
                        return;
                    }
                    window->statusBar()->showMessage(
                        QString("项目已保存: %1 (%2 张图片, %3 个目录)")
                            .arg(QFileInfo(filePath).fileName())
                            .arg(static_cast<int>(state->imageCount))
                            .arg(static_cast<int>(state->folderCount)));
                },
                Qt::QueuedConnection);
        });
    if (!m_persistenceTask)
    {
        m_persistenceCancel.reset();
        QMessageBox::warning(this, "保存项目", "后台保存任务被调度器拒绝。");
    }
}

void MainWindow::openProject()
{
    const QString filePath =
        QFileDialog::getOpenFileName(this, "打开项目", QString(), "MViewer 项目 (*.mvproj)");
    if (filePath.isEmpty())
        return;
    openProjectFile(filePath);
}

void MainWindow::openWorkspaceFile(const QString &filePath)
{
    if (filePath.isEmpty())
        return;
    // M47: the file read + JSON deserialize run on a background worker; the UI
    // only applies the parsed document atomically for the CURRENT generation.
    // A newer open supersedes an in-flight one (its delivery is dropped even
    // if the worker already finished); a read/parse failure never touches the
    // live session — the error is reported and the current state stays.
    TaskScheduler::cancel(m_restoreTask);
    m_restoreTask.reset();
    const uint64_t generation = ++m_restoreGeneration;
    auto result = std::make_shared<RestoreFileResult>();
    QPointer<MainWindow> guard(this);
    statusBar()->showMessage(QStringLiteral("正在打开工作区…"));
    auto handle = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [filePath, result](const TaskScheduler::TaskContext &ctx)
        { runRestoreFile(filePath, false, ctx, *result); },
        {}, std::chrono::steady_clock::time_point::max(),
        [guard, result, generation]()
        {
            if (!qApp)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, result, generation]()
                {
                    MainWindow *window = guard.data();
                    if (!window || generation != window->m_restoreGeneration)
                        return; // superseded or destroyed — never touch the session
                    window->m_restoreTask.reset();
                    if (!result->ok)
                    {
                        QMessageBox::critical(window, "打开工作区", result->error);
                        window->statusBar()->showMessage(
                            QStringLiteral("打开工作区失败: %1").arg(result->error));
                        return; // the live session is untouched
                    }
                    window->restoreWorkspaceState(result->workspace, result->filePath);
                },
                Qt::QueuedConnection);
        });
    if (!handle)
    {
        // submit() refused the task (pool paused / back-pressured): keep the
        // current session untouched and tell the user.
        QMessageBox::warning(this, "打开工作区", "后台打开任务被调度器拒绝。");
        return;
    }
    m_restoreTask = handle;
}

void MainWindow::openProjectFile(const QString &filePath)
{
    if (filePath.isEmpty())
        return;
    // Same async contract as openWorkspaceFile(); the project's embedded
    // workspace restores through the shared workspace-apply path.
    TaskScheduler::cancel(m_restoreTask);
    m_restoreTask.reset();
    const uint64_t generation = ++m_restoreGeneration;
    auto result = std::make_shared<RestoreFileResult>();
    QPointer<MainWindow> guard(this);
    statusBar()->showMessage(QStringLiteral("正在打开项目…"));
    auto handle = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [filePath, result](const TaskScheduler::TaskContext &ctx)
        { runRestoreFile(filePath, true, ctx, *result); },
        {}, std::chrono::steady_clock::time_point::max(),
        [guard, result, generation]()
        {
            if (!qApp)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, result, generation]()
                {
                    MainWindow *window = guard.data();
                    if (!window || generation != window->m_restoreGeneration)
                        return; // superseded or destroyed — never touch the session
                    window->m_restoreTask.reset();
                    if (!result->ok)
                    {
                        QMessageBox::critical(window, "打开项目", result->error);
                        window->statusBar()->showMessage(
                            QStringLiteral("打开项目失败: %1").arg(result->error));
                        return; // the live session is untouched
                    }
                    window->restoreWorkspaceState(result->workspace, result->filePath);
                    window->statusBar()->showMessage(
                        QString("项目已打开: %1 (%2 张图片, %3 个目录)")
                            .arg(QFileInfo(result->filePath).fileName())
                            .arg(static_cast<int>(result->workspace.imageCount()))
                            .arg(static_cast<int>(result->workspace.folderCount())));
                },
                Qt::QueuedConnection);
        });
    if (!handle)
    {
        QMessageBox::warning(this, "打开项目", "后台打开任务被调度器拒绝。");
        return;
    }
    m_restoreTask = handle;
}

// ─── P0: product browse state (recent / favorites / history / restore) ────────

void MainWindow::restoreLastSession()
{
    // Defer to the next event loop tick so the thumbnail worker has started and
    // setDirectory() has populated items before we try to scroll/select.
    QMetaObject::invokeMethod(
        this,
        [this]()
        {
            // P1-3: restore window layout (splitter + view mode) before populating widgets.
            QSettings settings;
            if (m_mainSplitter)
                m_mainSplitter->restoreState(settings.value("splitterState").toByteArray());
            const int vm = settings.value("thumbViewMode", ThumbnailPanel::Thumbnail).toInt();
            if (m_thumbnailPanel)
                m_thumbnailPanel->setViewMode(static_cast<ThumbnailPanel::ViewMode>(vm));
            const int ts = settings.value("thumbSize", ThumbnailPanel::kDefaultThumbSize).toInt();
            if (m_thumbnailPanel)
                m_thumbnailPanel->setThumbSize(ts);
            if (m_thumbSizeSlider)
                m_thumbSizeSlider->setValue(m_thumbnailPanel ? m_thumbnailPanel->thumbSize() : ts);

            // P1-3: restore the Analysis workspace so the UI reopens where left off.
            if (m_analysisPanel)
            {
                m_analysisPanel->setVisible(m_appState.analysisVisible);
                if (m_actToggleAnalysis)
                    m_actToggleAnalysis->setChecked(m_appState.analysisVisible);
                m_analysisPanel->setCurrentPage(m_appState.analysisPage);
            }
            // Restore search panel visibility.
            const bool searchVisible = settings.value("searchVisible", false).toBool();
            if (m_searchPanel)
                m_searchPanel->setVisible(searchVisible);
            if (m_actToggleSearch)
                m_actToggleSearch->setChecked(searchVisible);

            const QString dir = m_appState.lastDir;
            if (dir.isEmpty() || !QDir(dir).exists())
                return;
            changeDirectory(dir);

            const QString img = m_appState.lastImage;
            if (!img.isEmpty() && QFile::exists(img))
            {
                pushHistory(img);
                m_selection->setCurrentImage(img);
                m_imageViewer->setImage(img);  // async; imageReady() feeds AnalysisPanel
                m_previewPanel->setImage(img); // async; off UI thread
                m_metadataPanel->setImage(img);
                if (m_metadataOverlay)
                    m_metadataOverlay->setImage(img);
            }

            // P1-3: restore the full navigation history stack (browser back/forward
            // + History sidebar) so reopening lands the user mid-browse, not just
            // on the last image. Drop entries whose files no longer exist.
            QStringList restoredHist;
            for (const QString &p : m_appState.navHistory)
                if (QFile::exists(p))
                    restoredHist.append(p);
            if (!restoredHist.isEmpty())
            {
                m_history = restoredHist;
                int idx = m_appState.navHistoryIndex;
                if (idx < 0 || idx >= m_history.size())
                    idx = m_history.size() - 1;
                m_historyIndex = idx;
                // Feed the History sidebar panel from the restored stack.
                m_appState.history = m_history;
                updateSelectionActions();
            }
            // Restore the thumbnail-grid scroll position after items exist.
            QMetaObject::invokeMethod(
                this,
                [this]()
                {
                    if (m_appState.lastThumbScroll > 0)
                        m_thumbnailPanel->verticalScrollBar()->setValue(m_appState.lastThumbScroll);
                    if (!m_appState.lastImage.isEmpty())
                        m_thumbnailPanel->scrollToPath(m_appState.lastImage);

                    // A-6.3: restore viewer zoom/pan from QSettings (same logic as
                    // crash-recovery path, but for normal session restore).
                    QSettings vs;
                    if (m_imageViewer && !currentImagePath().isEmpty() &&
                        vs.value("viewerPath").toString() == currentImagePath())
                    {
                        Viewport v;
                        v.screenW = m_imageViewer->width();
                        v.screenH = m_imageViewer->height();
                        v.scale = vs.value("viewerScale", 1.0).toReal();
                        v.offsetX = vs.value("viewerOffX", 0.0).toReal();
                        v.offsetY = vs.value("viewerOffY", 0.0).toReal();
                        m_imageViewer->setViewTransform(v);
                    }

                    // A-6.1: restore Compare session on normal startup (not just
                    // crash recovery). If QSettings has a compareSession, reopen it.
                    const QJsonArray cmpImgs = vs.value("compareImages").toJsonArray();
                    const QString cmpSession = vs.value("compareSession").toString();
                    QStringList cmpPaths;
                    for (const auto &v2 : cmpImgs)
                    {
                        const QString p = v2.toString();
                        if (!p.isEmpty() && QFile::exists(p))
                            cmpPaths.append(p);
                    }
                    if (cmpPaths.size() >= 2 && !cmpSession.isEmpty())
                        openCompare(cmpPaths, cmpSession);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Persist browse position for next launch (P0 cross-session restore).
    m_appState.lastDir = currentDir();
    m_appState.lastImage = currentImagePath();
    m_appState.lastThumbScroll = m_thumbnailPanel ? m_thumbnailPanel->scrollOffset() : 0;

    // P1-3: persist the Analysis workspace so reopening restores UI state.
    // Focus Browse temporarily hides panels; persist the state from before that
    // temporary mode so closing in Focus does not silently change preferences.
    // M25: same rule for the Browse workspace — closing while Browse is active
    // must restore the analysis/search state the user had BEFORE entering
    // Browse, not the hidden Browse layout.
    const bool inBrowseWorkspace = m_actBrowseWorkspace && m_actBrowseWorkspace->isChecked();
    m_appState.analysisVisible =
        inBrowseWorkspace
            ? m_browseAnalysisVisible
            : (m_focusBrowse ? m_focusAnalysisVisible
                             : (m_analysisPanel && m_analysisPanel->isVisible()));
    m_appState.analysisPage = m_analysisPanel ? m_analysisPanel->currentPage() : 0;

    // P1-3: persist the navigation history stack (browser back/forward + History
    // panel) so reopening restores exactly where the user was browsing.
    m_appState.navHistory = m_history;
    m_appState.navHistoryIndex = m_historyIndex;
    m_appState.save();

    // M16: persist analysis history / pinned results so they survive restart.
    if (m_analyzer)
        m_analyzer->save();

    // Normal exit: remove the crash-recovery marker so the next launch doesn't
    // prompt for a restore (only an unclean shutdown leaves it behind).
    {
        const QString recoveryPath = appConfigFile(QStringLiteral("recovery.json"));
        QFile::remove(recoveryPath);
    }

    // Persist the recent-folders LRU alongside app state.
    const QString recentPath = appConfigFile(QStringLiteral("recent.json"));
    QSaveFile rf(recentPath);
    if (!recentPath.isEmpty() && rf.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (rf.write(QByteArray::fromStdString(m_recent.serialize())) >= 0)
            (void)rf.commit();
    }

    // M13.5 / P1-3: persist window geometry/layout (QSettings, independent of workspace).
    {
        QSettings settings;
        settings.setValue("geometry", saveGeometry());
        settings.setValue("windowState", saveState());
        // P1-3: persist thumbnail view mode and splitter geometry.
        if (m_thumbnailPanel)
            settings.setValue("thumbViewMode", m_thumbnailPanel->viewMode());
        if (m_thumbnailPanel)
            settings.setValue("thumbSize", m_thumbnailPanel->thumbSize());
        if (m_sortCombo)
            settings.setValue("thumbSortMode", m_sortCombo->currentData().toInt());
        if (m_mainSplitter)
            settings.setValue("splitterState", m_mainSplitter->saveState());
        if (m_searchPanel && m_actToggleSearch)
        {
            const bool searchVisible =
                inBrowseWorkspace
                    ? m_browseSearchVisible
                    : (m_focusBrowse ? m_focusSearchVisible : m_searchPanel->isVisible());
            settings.setValue("searchVisible", searchVisible);
        }
        // P1-7: persist the main viewer's zoom level + pan position so a session
        // that ended with the viewer open restores identically (scale/offset are
        // screen-space, so the viewer must have been visible to be meaningful).
        if (m_imageViewer && !m_imageViewer->isHidden() && !currentImagePath().isEmpty())
        {
            const auto v = m_imageViewer->viewTransform();
            settings.setValue("viewerPath", currentImagePath());
            settings.setValue("viewerScale", v.scale);
            settings.setValue("viewerOffX", v.offsetX);
            settings.setValue("viewerOffY", v.offsetY);
        }
        // A-6.1: persist Compare session for normal startup restore (not just
        // crash recovery). Same format as autosaveSession().
        if (m_compareView && m_compareView->comparedImageCount() >= 2)
        {
            const auto cs = m_compareView->compareSession();
            QJsonArray cmpImg;
            for (const auto &id : cs.imageIds)
                cmpImg.append(QString::fromUtf8(id.data(), static_cast<int>(id.size())));
            settings.setValue("compareImages", cmpImg);
            settings.setValue("compareSession",
                              QString::fromStdString(mviewer::core::serializeCompareSession(cs)));
        }
        else
        {
            settings.remove("compareImages");
            settings.remove("compareSession");
        }
        // A-6.4: persist left-column width (main splitter index 0) as a plain
        // int so it can be restored even when analysis/search visibility changes.
        if (m_mainSplitter)
        {
            const QList<int> sizes = m_mainSplitter->sizes();
            if (!sizes.isEmpty())
                settings.setValue("navSidebarWidth", sizes[0]);
        }
        // A-6.4: persist vertical proportions of the left sidebar independently.
        if (m_leftSplitter)
            settings.setValue("leftSplitterState", m_leftSplitter->saveState());
        settings.sync();
    }

    QMainWindow::closeEvent(event);
}

// M15: crash recovery — autosave current session to a recovery file.
void MainWindow::autosaveSession()
{
    if (currentDir().isEmpty() && currentImagePath().isEmpty())
        return;
    const QString recoveryPath = appConfigFile(QStringLiteral("recovery.json"));
    if (recoveryPath.isEmpty())
        return;
    QSaveFile f(recoveryPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    // Simple JSON: lastDir, lastImage, lastThumbScroll, compare (M15 P0#1)
    QJsonObject obj;
    obj.insert("lastDir", currentDir());
    obj.insert("lastImage", currentImagePath());
    obj.insert("lastThumbScroll", m_thumbnailPanel ? m_thumbnailPanel->scrollOffset() : 0);

    // M15 P0#1: also persist the live Compare session (images + full state) so a
    // crash can restore Compare, not just the gallery/single view.
    if (m_compareView && m_compareView->comparedImageCount() >= 2)
    {
        const auto cs = m_compareView->compareSession();
        QJsonArray cmpImg;
        for (const auto &id : cs.imageIds)
            cmpImg.append(QString::fromUtf8(id.data(), static_cast<int>(id.size())));
        obj.insert("compareImages", cmpImg);
        obj.insert("compareSession",
                   QString::fromStdString(mviewer::core::serializeCompareSession(cs)));
    }

    obj.insert("timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    QJsonDocument doc(obj);
    if (f.write(doc.toJson()) >= 0)
        (void)f.commit();
}

// M15: crash recovery — restore session from recovery file if it exists.
void MainWindow::restoreSessionRecovery()
{
    const QString recoveryPath = appConfigFile(QStringLiteral("recovery.json"));
    if (recoveryPath.isEmpty())
        return;
    QFile f(recoveryPath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return;
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (doc.isNull() || !doc.isObject())
        return;

    const QJsonObject obj = doc.object();
    const QString lastDir = obj.value("lastDir").toString();
    const QString lastImage = obj.value("lastImage").toString();
    const int lastThumbScroll = obj.value("lastThumbScroll").toInt();
    const QJsonArray compareImages = obj.value("compareImages").toArray();
    const QString compareSession = obj.value("compareSession").toString();

    if (lastDir.isEmpty() && lastImage.isEmpty() && compareImages.isEmpty())
        return;

    // Ask the user whether to restore the previous session. The recovery file
    // is a crash-recovery artifact; a normal exit clears it (see closeEvent),
    // so its presence implies an unclean shutdown.
    const auto answer = QMessageBox::question(
        this, tr("恢复上次会话"), tr("检测到上次会话未正常关闭。\n是否恢复上次浏览的图片和目录？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes)
    {
        QFile::remove(recoveryPath);
        return;
    }

    // M15 P0#1: restore the Compare session too. Only trust it if the recorded
    // images still exist on disk.
    QStringList cmpImgs;
    for (const auto &v : compareImages)
    {
        const QString p = v.toString();
        if (!p.isEmpty() && QFile::exists(p))
            cmpImgs.append(p);
    }
    const bool restoreCompare = cmpImgs.size() >= 2 && !compareSession.isEmpty();

    // Restore the session (deferred to event loop).
    QTimer::singleShot(
        100, this,
        [this, lastDir, lastImage, lastThumbScroll, cmpImgs, compareSession, restoreCompare]()
        {
            if (!lastDir.isEmpty() && QDir(lastDir).exists())
            {
                changeDirectory(lastDir);
                if (lastThumbScroll > 0)
                    m_thumbnailPanel->verticalScrollBar()->setValue(lastThumbScroll);
            }
            if (!lastImage.isEmpty() && QFile::exists(lastImage))
            {
                m_selection->setCurrentImage(lastImage);
                onImageOpen(lastImage);
                // P1-7: if the session ended with the viewer open on
                // this exact image, restore its zoom level + pan. The
                // transform is applied on the UI thread after the async
                // decode completes (see ImageViewer::setImage).
                QSettings vs;
                if (vs.value("viewerPath").toString() == lastImage)
                {
                    Viewport v;
                    v.screenW = m_imageViewer->width();
                    v.screenH = m_imageViewer->height();
                    v.scale = vs.value("viewerScale", 1.0).toReal();
                    v.offsetX = vs.value("viewerOffX", 0.0).toReal();
                    v.offsetY = vs.value("viewerOffY", 0.0).toReal();
                    m_imageViewer->setViewTransform(v);
                }
            }
            // M15 P0#1: reopen Compare with its fully persisted
            // session (ROI, zoom, layout, threshold, blink, ...).
            if (restoreCompare)
                openCompare(cmpImgs, compareSession);
            m_autosaveLoaded = true;
        });
}
