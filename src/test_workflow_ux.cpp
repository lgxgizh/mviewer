// Beta 回归 — 用户 Workflow 端到端测试（2026-07 外部评审落地）。
//
// 评审要求：不要只验证 "Compare 能不能打开"，而要验证 "Compare 好不好用"；
// 每一条完整用户流程（Workflow）都要走一遍，检查等待/状态不同步/缩放错误。
//
// Workflow 1（浏览主流程）——真实 MainWindow：
//   打开目录 → 浏览图片 → 键盘切换（←/→/Home/End/PageUp/PageDown）
//   → 放大（100%/放大）→ 恢复（Fit）→ 关闭。
//   断言点：SelectionModel（SSOT）与导航状态始终同步、缩放数值正确、边界不越界。
//
// Workflow 2（比较主流程）——真实 CompareWorkspace + 真实键盘事件：
//   两图 Compare → 模式切换：闪烁(B)/左右分割(S)/叠加(O)/棋盘(K)/差异高亮(H)
//   → 互斥模式不残留（评审"状态不同步"检查）→ Space 临时闪烁
//   → Esc 退出 Compare（QDialog 真实路径）→ 继续浏览。
//
// 与 docs/beta_checklist.md 的 "浏览体验 / Compare / View" 条目一一对应。

#include "appstate.h"
#include "compareworkspace.h"
#include "core/cache/CacheManager.h"
#include "core/compare/DifferenceEngine.h"
#include "core/image/Decoder.h"
#include "core/image/ImageAdjust.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageRepository.h"
#include "core/image/QtConvert.h"
#include "core/metadata/MetadataIndexer.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "directorymodel.h"
#include "directorytree.h"
#include "exportdialog.h"
#include "imageviewer.h"
#include "mainwindow.h"
#include "metadataoverlay.h"
#include "previewpanel.h"
#include "searchpanel.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"
#include "widgets/histogramwidget.h"
#include "widgets/rawimageview.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace
{
int g_failures = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            std::cout << "[ok] " << msg << "\n";                                                   \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n";         \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

void pump(int ms = 50)
{
    QElapsedTimer t;
    t.start();
    do
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    } while (t.elapsed() < ms);
}

void sendKey(QWidget *w, Qt::Key key, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QKeyEvent press(QEvent::KeyPress, key, mods);
    QApplication::sendEvent(w, &press);
    QKeyEvent release(QEvent::KeyRelease, key, mods);
    QApplication::sendEvent(w, &release);
    pump(20);
}

void sendFocusedKey(Qt::Key key, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QWidget *target = QApplication::focusWidget();
    if (!target)
        return;
    QKeyEvent press(QEvent::KeyPress, key, mods);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, mods);
    QApplication::sendEvent(target, &release);
    pump(20);
}

// 只发 KeyPress（Space 按住场景）。
void sendKeyPressOnly(QWidget *w, Qt::Key key)
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(w, &press);
    pump(20);
}

void sendKeyReleaseOnly(QWidget *w, Qt::Key key)
{
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QApplication::sendEvent(w, &release);
    pump(20);
}

void sendMouseMove(QWidget *w, const QPoint &point)
{
    QMouseEvent event(QEvent::MouseMove, QPointF(point), Qt::NoButton, Qt::NoButton,
                      Qt::NoModifier);
    QApplication::sendEvent(w, &event);
    pump(20);
}

void sendLeftClick(QWidget *w, const QPoint &point)
{
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(point), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(w, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(point), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(w, &release);
    pump(20);
}

// ── M29: Compare diff overlays + metrics are computed asynchronously as ONE
// AnalysisPool batch per refresh request. These helpers wait for the delivery
// to settle so the assertions observe delivered state, never in-flight work.
void waitForAnalysisIdle(int timeoutMs = 8000)
{
    QElapsedTimer t;
    t.start();
    for (;;)
    {
        pump(10);
        const auto m = TaskScheduler::instance().metrics(TaskScheduler::PoolType::AnalysisPool);
        if (m.pending == 0 && m.active_tasks == 0 && m.queue_depth == 0)
        {
            pump(30); // apply any diff delivery already queued for the UI thread
            return;
        }
        if (t.elapsed() >= timeoutMs)
            return;
    }
}

// Release-gated Analysis blocker: occupies the single Analysis worker until
// the destructor releases it. The task captures only the shared release state
// (the Control), never this helper — so there is no ownership cycle.
struct AnalysisBlocker
{
    struct Control
    {
        std::mutex mtx;
        std::condition_variable cv;
        bool released = false;
    };
    std::shared_ptr<Control> control = std::make_shared<Control>();
    TaskScheduler::TaskHandle task;

    AnalysisBlocker()
    {
        auto c = control;
        task = TaskScheduler::instance().submit(
            TaskScheduler::Priority::Analysis,
            [c](const TaskScheduler::TaskContext &)
            {
                std::unique_lock<std::mutex> lk(c->mtx);
                c->cv.wait(lk, [c] { return c->released; });
            });
    }
    ~AnalysisBlocker()
    {
        std::lock_guard<std::mutex> lk(control->mtx);
        control->released = true;
        control->cv.notify_all();
    }
    AnalysisBlocker(const AnalysisBlocker &) = delete;
    AnalysisBlocker &operator=(const AnalysisBlocker &) = delete;
};

QString waitForMetricsChange(QLabel *label, const QString &previous, int timeoutMs = 8000)
{
    QElapsedTimer t;
    t.start();
    while (label->text() == previous && t.elapsed() < timeoutMs)
        pump(25);
    return label->text();
}

void waitForMetricsText(QLabel *label, const QString &expected, int timeoutMs = 8000)
{
    QElapsedTimer t;
    t.start();
    while (label->text() != expected && t.elapsed() < timeoutMs)
        pump(25);
}

QString writePng(const QDir &dir, const QString &name, QColor color, int width = 32,
                 int height = 32)
{
    const QString path = dir.filePath(name);
    QImage img(width, height, QImage::Format_RGB32);
    img.fill(color);
    img.save(path, "PNG");
    return path;
}

// Minimal little-endian TIFF (DNG-like) carrying ISO/Make/Model/LensModel so
// parseRawMetadata extracts real camera/lens/ISO fields.
bool writeFakeDng(const std::string &path, const std::string &make, const std::string &model,
                  const std::string &lens, uint16_t iso)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    uint8_t hdr[8] = {'I', 'I', 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00};
    std::fwrite(hdr, 1, 8, f);
    const uint16_t count = 4;
    std::fwrite(&count, 2, 1, f);
    const long ifdStart = 8;
    long dataOffset = ifdStart + 2 + count * 12 + 4;
    const long makeOff = dataOffset;
    const long modelOff = makeOff + static_cast<long>(make.size()) + 1;
    const long lensOff = modelOff + static_cast<long>(model.size()) + 1;
    auto writeEntry = [&](uint16_t tag, uint16_t type, uint32_t cnt, uint32_t val)
    {
        std::fwrite(&tag, 2, 1, f);
        std::fwrite(&type, 2, 1, f);
        std::fwrite(&cnt, 4, 1, f);
        std::fwrite(&val, 4, 1, f);
    };
    writeEntry(0x8827, 3, 1, iso); // ISO (SHORT inline)
    writeEntry(0x010F, 2, static_cast<uint32_t>(make.size()) + 1,
               static_cast<uint32_t>(makeOff)); // Make (ASCII)
    writeEntry(0x0110, 2, static_cast<uint32_t>(model.size()) + 1,
               static_cast<uint32_t>(modelOff)); // Model (ASCII)
    writeEntry(0xA434, 2, static_cast<uint32_t>(lens.size()) + 1,
               static_cast<uint32_t>(lensOff)); // LensModel (ASCII)
    uint32_t nextIfd = 0;
    std::fwrite(&nextIfd, 4, 1, f);
    std::fseek(f, makeOff, SEEK_SET);
    std::fwrite(make.c_str(), 1, make.size() + 1, f);
    std::fwrite(model.c_str(), 1, model.size() + 1, f);
    std::fwrite(lens.c_str(), 1, lens.size() + 1, f);
    std::fclose(f);
    return true;
}

// 按文本前缀查找 CompareWorkspace 的模式复选框（成员是私有的，文本是公共 UI 契约）。
QCheckBox *findChk(QWidget *root, const QString &textPrefix)
{
    const auto boxes = root->findChildren<QCheckBox *>();
    for (QCheckBox *c : boxes)
        if (c->text().startsWith(textPrefix))
            return c;
    return nullptr;
}

QPushButton *findBtn(QWidget *root, const QString &textPrefix)
{
    const auto buttons = root->findChildren<QPushButton *>();
    for (QPushButton *button : buttons)
        if (button->isCheckable() && button->text().startsWith(textPrefix))
            return button;
    return nullptr;
}

// ─── Workflow 1: 打开目录 → 浏览 → 键盘切换 → 放大 → 恢复 → 关闭 ─────────────
void workflow1_browse(const QString &dirPath, const QStringList &paths)
{
    // Reproduce the real upgrade path: the former sidebar persisted a splitter
    // with six independent children. Qt may partially apply that byte array to
    // the new three-section splitter unless the one-time migration replaces it.
    QSettings settings;
    settings.remove("browserSidebarLayoutVersion");
    {
        QSplitter legacySidebar(Qt::Vertical);
        for (int i = 0; i < 6; ++i)
            legacySidebar.addWidget(new QWidget(&legacySidebar));
        legacySidebar.resize(300, 700);
        legacySidebar.setSizes({20, 320, 160, 20, 20, 500});
        settings.setValue("leftSplitterState", legacySidebar.saveState());
    }

    std::cout << "── Workflow 1: browse ──\n";

    // A second folder makes the directory-switch contract observable: the
    // old selection must clear before the asynchronous first row arrives.
    QDir sourceDir(dirPath);
    const QString nextDir = sourceDir.filePath(QStringLiteral("wf_next"));
    sourceDir.mkpath(QStringLiteral("wf_next"));
    const QString nextPath =
        writePng(QDir(nextDir), QStringLiteral("next.png"), QColor(80, 120, 220));

    const QString recoveryPath =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/recovery.json";
    QDir().mkpath(QFileInfo(recoveryPath).absolutePath());
    QFile recoveryFile(recoveryPath);
    CHECK(recoveryFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
          "recovery fixture is written to the test AppConfig");
    if (recoveryFile.isOpen())
    {
        QJsonObject recovery;
        recovery.insert("lastDir", dirPath);
        recovery.insert("lastImage", paths.first());
        recoveryFile.write(QJsonDocument(recovery).toJson());
        recoveryFile.close();
    }

    MainWindow *window = nullptr;
    bool recoveryPromptSeen = false;
    bool recoveryParentVisible = false;
    bool recoveryParentMatches = false;
    QTimer recoveryPromptPoller;
    recoveryPromptPoller.setInterval(10);
    QObject::connect(&recoveryPromptPoller, &QTimer::timeout,
                     [&]()
                     {
                         for (QWidget *top : QApplication::topLevelWidgets())
                         {
                             auto *box = qobject_cast<QMessageBox *>(top);
                             if (!box || !box->isVisible())
                                 continue;
                             recoveryPromptSeen = true;
                             recoveryParentVisible = window && window->isVisible();
                             recoveryParentMatches = window && box->parentWidget() == window;
                             box->done(QMessageBox::No);
                             recoveryPromptPoller.stop();
                             return;
                         }
                     });
    recoveryPromptPoller.start();

    QElapsedTimer constructionTimer;
    constructionTimer.start();
    MainWindow w;
    window = &w;
    const qint64 constructionMs = constructionTimer.elapsed();
    CHECK(constructionMs < 5000, "recovery prompt does not block MainWindow construction");
    CHECK(QApplication::activeModalWidget() == nullptr,
          "recovery prompt is not modal during MainWindow construction");
    w.resize(1280, 800);
    w.show();
    pump(100);
    CHECK(recoveryPromptSeen, "recovery prompt appears after the main window is shown");
    CHECK(recoveryParentMatches && recoveryParentVisible,
          "recovery prompt is parented to the visible MainWindow");
    w.activateWindow();
    w.raise();
    pump(10);

    auto *analysisPanel = w.findChild<QWidget *>("analysisPanel");
    auto *searchPanel = w.findChild<QWidget *>("searchPanel");
    auto *navigationPanel = w.findChild<QWidget *>("navigationPanel");
    auto *advancedFilterPanel = w.findChild<QWidget *>("advancedFilterPanel");
    auto *advancedFilterToggle = w.findChild<QPushButton *>("advancedFilterToggle");
    auto *analysisAction = w.findChild<QAction *>("toggleAnalysisPanelAction");
    auto *searchAction = w.findChild<QAction *>("toggleSearchPanelAction");
    auto *focusAction = w.findChild<QAction *>("focusBrowseAction");
    auto *browserToolBar = w.findChild<QToolBar *>("browserToolBar");
    auto *browseWorkspaceAction = w.findChild<QAction *>("browseWorkspaceAction");
    auto *foldersSection = w.findChild<QWidget *>("foldersSection");
    auto *previewSection = w.findChild<QWidget *>("previewSection");
    auto *directoryTree = w.findChild<DirectoryTree *>();
    auto *previewPanel = w.findChild<PreviewPanel *>();
    CHECK(browserToolBar != nullptr, "professional browser toolbar has a stable object name");
    CHECK(browserToolBar && browserToolBar->actions().size() >= 8,
          "browser toolbar exposes the high-frequency browse commands");
    CHECK(browseWorkspaceAction && browseWorkspaceAction->isCheckable(),
          "browse layout action is a stable checkable command");
    CHECK(foldersSection && previewSection,
          "navigation sidebar exposes stable folders and preview sections");
    CHECK(settings.value("browserSidebarLayoutVersion").toInt() == 1,
          "legacy navigation sidebar layout is migrated exactly once");
    CHECK(foldersSection && previewSection && foldersSection->height() > previewSection->height(),
          "migrated sidebar gives the folder section the larger 60/40 share");
    CHECK(directoryTree && directoryTree->filterEdit() &&
              directoryTree->filterEdit()->height() <= 40,
          "directory filter keeps a normal editor height after layout restore");
    CHECK(directoryTree && directoryTree->isVisible() && directoryTree->height() >= 40,
          "directory tree remains visible with usable height");
    CHECK(previewPanel && previewPanel->isVisible() && previewPanel->height() >= 80,
          "preview panel remains visible with usable height");
    CHECK(analysisPanel && !analysisPanel->isVisible(),
          "clean startup keeps the analysis panel hidden");
    CHECK(searchPanel && !searchPanel->isVisible(), "clean startup keeps the search panel hidden");
    CHECK(advancedFilterPanel && !advancedFilterPanel->isVisible(),
          "advanced gallery filters are hidden by default");
    CHECK(advancedFilterToggle && advancedFilterToggle->isCheckable(),
          "advanced filter toggle has a stable interactive control");

    auto *thumbnailSizeSlider = w.findChild<QSlider *>("thumbnailSizeSlider");
    auto *viewModeCombo = w.findChild<QComboBox *>("thumbnailViewModeCombo");
    auto *thumbnailPanel = w.findChild<ThumbnailPanel *>();
    auto *pathEdit = w.findChild<QLineEdit *>("pathEdit");
    CHECK(thumbnailSizeSlider != nullptr, "thumbnail size slider is discoverable");
    CHECK(viewModeCombo && thumbnailPanel,
          "thumbnail view controls expose a stable panel and combo");
    CHECK(pathEdit != nullptr, "directory path input is discoverable");
    auto *emptyState = w.findChild<QLabel *>("emptyStateLabel");
    auto *emptyFolder = w.findChild<QLabel *>("emptyFolderLabel");
    CHECK(emptyState != nullptr, "empty-state hint has a stable object name");
    CHECK(emptyFolder != nullptr, "empty-folder hint has a stable object name");
    CHECK(emptyState && emptyState->isVisible(),
          "empty-state hint is shown when no directory is open");

    if (thumbnailSizeSlider && viewModeCombo && thumbnailPanel)
    {
        thumbnailSizeSlider->setValue(180);
        CHECK(thumbnailPanel->thumbSize() == 180 && thumbnailSizeSlider->value() == 180,
              "standard grid remembers the user-selected 180px size");
        auto setViewModeFromCombo = [viewModeCombo](ThumbnailPanel::ViewMode mode)
        {
            for (int i = 0; i < viewModeCombo->count(); ++i)
                if (viewModeCombo->itemData(i).toInt() == static_cast<int>(mode))
                {
                    viewModeCombo->setCurrentIndex(i);
                    return;
                }
        };
        setViewModeFromCombo(ThumbnailPanel::LargeIcon);
        CHECK(thumbnailPanel->viewMode() == ThumbnailPanel::LargeIcon &&
                  viewModeCombo->currentData().toInt() == ThumbnailPanel::LargeIcon &&
                  thumbnailSizeSlider->value() == 240,
              "Large view synchronizes mode and slider at 240px");
        setViewModeFromCombo(ThumbnailPanel::SmallIcon);
        CHECK(thumbnailPanel->viewMode() == ThumbnailPanel::SmallIcon &&
                  viewModeCombo->currentData().toInt() == ThumbnailPanel::SmallIcon &&
                  thumbnailSizeSlider->value() == 64,
              "Small view synchronizes mode and slider at 64px");
        setViewModeFromCombo(ThumbnailPanel::Thumbnail);
        CHECK(thumbnailPanel->viewMode() == ThumbnailPanel::Thumbnail &&
                  viewModeCombo->currentData().toInt() == ThumbnailPanel::Thumbnail &&
                  thumbnailSizeSlider->value() == 180 && thumbnailPanel->thumbSize() == 180,
              "returning to Thumbnail restores the remembered 180px grid");

        setViewModeFromCombo(ThumbnailPanel::LargeIcon);
        thumbnailPanel->setThumbSize(192); // Restore/preferences call after the mode.
        CHECK(thumbnailPanel->viewMode() == ThumbnailPanel::LargeIcon &&
                  thumbnailPanel->thumbSize() == 240 && thumbnailSizeSlider->value() == 240,
              "restore size cannot override the Large 240px preset");
        CHECK(!thumbnailSizeSlider->isEnabled(), "Large preset disables the thumbnail size slider");
        thumbnailPanel->setViewMode(ThumbnailPanel::LargeIcon);
        CHECK(viewModeCombo->currentData().toInt() == ThumbnailPanel::LargeIcon &&
                  thumbnailSizeSlider->value() == 240,
              "same-mode setViewMode re-synchronizes the effective Large size");
        setViewModeFromCombo(ThumbnailPanel::SmallIcon);
        CHECK(!thumbnailSizeSlider->isEnabled() && thumbnailSizeSlider->value() == 64,
              "Small preset disables the slider and keeps its 64px value");

        setViewModeFromCombo(ThumbnailPanel::Details);
        CHECK(!thumbnailSizeSlider->isEnabled(),
              "Details disables the thumbnail size slider because it has no visible effect");
        thumbnailPanel->setThumbSize(196); // Preferences call while Details is active.
        CHECK(thumbnailSizeSlider->value() == 196,
              "Details accepts a size for the future standard grid");
        setViewModeFromCombo(ThumbnailPanel::List);
        CHECK(!thumbnailSizeSlider->isEnabled(),
              "List disables the thumbnail size slider because it has no visible effect");
        setViewModeFromCombo(ThumbnailPanel::Details);
        setViewModeFromCombo(ThumbnailPanel::Thumbnail);
        CHECK(thumbnailPanel->thumbSize() == 196 && thumbnailSizeSlider->value() == 196,
              "Thumbnail restores the size remembered while Details was active");
        CHECK(thumbnailSizeSlider->isEnabled(), "Thumbnail re-enables the thumbnail size slider");
    }

    if (browseWorkspaceAction)
    {
        browseWorkspaceAction->setChecked(false);
        if (analysisAction && !analysisAction->isChecked())
            analysisAction->trigger();
        if (searchAction && searchAction->isChecked())
            searchAction->trigger();
        browseWorkspaceAction->setChecked(true);
        pump(10);
        CHECK(navigationPanel && navigationPanel->isVisible() && analysisPanel &&
                  !analysisPanel->isVisible() && searchPanel && !searchPanel->isVisible() &&
                  browseWorkspaceAction->isChecked(),
              "browse layout keeps navigation and gives the gallery the available space");
        browseWorkspaceAction->trigger();
        pump(10);
        CHECK(analysisPanel && analysisPanel->isVisible() && searchPanel &&
                  !searchPanel->isVisible() && browseWorkspaceAction &&
                  !browseWorkspaceAction->isChecked(),
              "leaving browse layout strictly restores the recorded panel state");
        browseWorkspaceAction->trigger();
        pump(10);
    }

    if (analysisAction)
        analysisAction->trigger();
    CHECK(analysisPanel && analysisPanel->isVisible() && analysisAction &&
              analysisAction->isChecked(),
          "analysis action and panel visibility stay synchronized");
    CHECK(searchPanel && !searchPanel->isVisible(), "search panel remains independently hidden");

    if (thumbnailSizeSlider)
        thumbnailSizeSlider->setFocus(Qt::OtherFocusReason);
    pump(10);
    CHECK(QApplication::focusWidget() == thumbnailSizeSlider,
          "focus can be placed on the real thumbnail size slider");
    sendFocusedKey(Qt::Key_Tab);
    CHECK(navigationPanel && !navigationPanel->isVisible() && analysisPanel &&
              !analysisPanel->isVisible() && searchPanel && !searchPanel->isVisible() &&
              focusAction && focusAction->isChecked(),
          "plain Tab from a focused child enters focus browse");
    CHECK(analysisAction && !analysisAction->isEnabled() && searchAction &&
              !searchAction->isEnabled(),
          "focus browse disables panel toggles while their state is suspended");

    if (pathEdit)
        pathEdit->setFocus(Qt::OtherFocusReason);
    pump(10);
    CHECK(QApplication::focusWidget() == pathEdit,
          "focus can be placed on the real directory path input");
    sendFocusedKey(Qt::Key_Tab);
    CHECK(navigationPanel && navigationPanel->isVisible() && analysisPanel &&
              analysisPanel->isVisible() && searchPanel && !searchPanel->isVisible() &&
              focusAction && !focusAction->isChecked(),
          "plain Tab from a focused path input exits focus browse");
    CHECK(analysisAction && analysisAction->isEnabled() && analysisAction->isChecked() &&
              searchAction && searchAction->isEnabled() && !searchAction->isChecked(),
          "leaving focus browse restores action state as well as panels");

    if (pathEdit)
        pathEdit->setFocus(Qt::OtherFocusReason);
    pump(10);
    sendFocusedKey(Qt::Key_Tab, Qt::ShiftModifier);
    CHECK(focusAction && !focusAction->isChecked(),
          "Shift+Tab keeps normal focus traversal and does not enter focus browse");
    if (analysisAction)
        analysisAction->trigger();

    auto *sel = w.findChild<SelectionModel *>();
    auto *dirModel = w.findChild<DirectoryModel *>();
    CHECK(sel != nullptr, "MainWindow exposes the SelectionModel SSOT");
    CHECK(dirModel != nullptr, "MainWindow exposes the DirectoryModel SSOT");
    CHECK(directoryTree != nullptr, "directory navigation tree is discoverable");
    if (!sel || !dirModel || !directoryTree)
        return;

    // ImageViewer 是独立的顶层窗口（MainWindow::setupUi 创建，onImageOpen 时 show）。
    // 整个测试进程只构造一次 MainWindow，setupUi 也只执行一次，因此这里抓到的
    // 一定是 onImageOpen 实际使用的那个 m_imageViewer（早期版本因重复 setupUi
    // 会泄漏第二个孤儿 viewer，导致抓取不确定而 flaky）。
    ImageViewer *viewer = nullptr;
    for (QWidget *top : QApplication::topLevelWidgets())
        if (auto *v = qobject_cast<ImageViewer *>(top))
            viewer = v;
    CHECK(viewer != nullptr, "image viewer window exists after setupUi");

    // 模拟 main() 的真实启动顺序：窗口先构造完成，再设置命令行/文件关联路径。
    // 事件循环必须驱动 queued open，不能直接调用 onImageOpen 绕过回归点。
    w.setOpenOnLaunch(paths[0]);
    QElapsedTimer launchTimer;
    launchTimer.start();
    while (
        (sel->currentImage() != paths[0] || !viewer || !viewer->isVisible() || !viewer->frame()) &&
        launchTimer.elapsed() < 5000)
        pump(25);
    CHECK(sel->currentImage() == paths[0],
          "post-construction setOpenOnLaunch syncs SelectionModel.currentImage");
    CHECK(viewer && viewer->isVisible(),
          "post-construction setOpenOnLaunch makes ImageViewer visible");
    CHECK(viewer && viewer->frame(), "post-construction setOpenOnLaunch decodes a frame within 5s");

    // 打开第一张图（真实产品入口 onImageOpen：双击文件 → 查看器）。
    // 注意顺序：先打开单图、等首屏，再打开目录。offscreen 测试平台不允许
    // 跨线程池并发全分辨率 QImageReader::read()（M3 已知平台限制，见
    // ImageRepository.cpp），因此测试按真实用户节奏逐步推进而不是并发轰炸。
    // Launch coverage above already opened the first image through the public
    // setter; do not call onImageOpen directly and hide a duplicate-open bug.
    pump(100);
    CHECK(sel->currentImage() == paths[0],
          "launch open keeps SelectionModel.currentImage synchronized");

    // Return in the editable path field must navigate without invoking the
    // window-level quick-preview command or reopening the old image.
    if (viewer)
        viewer->close();
    if (pathEdit)
    {
        pathEdit->setText(nextDir);
        pathEdit->setFocus(Qt::OtherFocusReason);
        sendFocusedKey(Qt::Key_Return);
        CHECK(viewer && !viewer->isVisible(),
              "Return with a path editor focused does not quick-preview the old image");
    }

    // Directory switch: clear stale SSOT selection synchronously, then select
    // the first new gallery item once the asynchronous scan publishes rows.
    sel->setCurrentImage(paths[0]);
    directoryTree->navigateTo(nextDir, true);
    CHECK(sel->currentImage().isEmpty(),
          "directory change clears the previous current image before scanning");
    QElapsedTimer firstSelectionTimer;
    firstSelectionTimer.start();
    while (sel->currentImage() != nextPath && firstSelectionTimer.elapsed() < 5000)
        pump(25);
    CHECK(sel->currentImage() == nextPath,
          "first item in a newly loaded directory is selected exactly through the SSOT");
    CHECK(emptyState && !emptyState->isVisible(),
          "empty-state hint hides once a directory is open");
    directoryTree->navigateTo(dirPath, true);
    pump(250);

    const QString externalDir = QDir(QStringLiteral(MVIEWER_SOURCE_DIR))
                                    .filePath(QStringLiteral("testdata/golden/256x256"));
    if (directoryTree->filterEdit())
        directoryTree->filterEdit()->setText(QStringLiteral("hide-target-until-navigation"));
    directoryTree->navigateTo(externalDir, true);
    QElapsedTimer externalNavigationTimer;
    externalNavigationTimer.start();
    while (directoryTree->currentPath() != QDir::cleanPath(externalDir) &&
           externalNavigationTimer.elapsed() < 5000)
        pump(25);
    CHECK(directoryTree->currentPath() == QDir::cleanPath(externalDir),
          "directory tree navigates to an existing path outside the home directory");
    CHECK(directoryTree->filterEdit() && directoryTree->filterEdit()->text().isEmpty(),
          "external navigation clears a filter that would hide the target");
    CHECK(directoryTree->currentIndex().isValid() &&
              directoryTree->currentIndex().data(Qt::DisplayRole).toString() == "256x256",
          "external directory navigation leaves the target visibly selected");

    // Empty-folder feedback: a directory without image files must show the
    // no-images hint, and it must disappear once a real folder is opened.
    const QString emptyDir = sourceDir.filePath(QStringLiteral("wf_empty"));
    QDir().mkpath(emptyDir);
    directoryTree->navigateTo(emptyDir, true);
    QElapsedTimer emptyDirTimer;
    emptyDirTimer.start();
    while ((!emptyFolder || !emptyFolder->isVisible()) && emptyDirTimer.elapsed() < 5000)
        pump(25);
    CHECK(emptyFolder && emptyFolder->isVisible(), "empty folder shows the no-images hint");
    directoryTree->navigateTo(dirPath, true);
    pump(300);
    CHECK(emptyFolder && !emptyFolder->isVisible(),
          "no-images hint hides once a folder with images is opened");

    // The F1 cheat sheet must stay in sync with the registered keyboard
    // commands (regression: Ctrl+F directory filter and F1 help were
    // missing from the sheet).
    const QString helpHtml = MainWindow::shortcutsHelpHtml();
    CHECK(helpHtml.contains("Ctrl+F"),
          "cheat sheet documents the Ctrl+F directory-filter shortcut");
    CHECK(helpHtml.contains("F1"), "cheat sheet documents the F1 help shortcut");
    if (viewer)
    {
        // 首屏：异步解码必须在 5 秒内交付（评审"有没有等待"硬上限）。
        QElapsedTimer t;
        t.start();
        while (!viewer->frame() && t.elapsed() < 5000)
            pump(25);
        CHECK(viewer->frame() != nullptr, "first image decodes within 5s");
    }

    // 打开目录（真实 SSOT 路径：DirectoryModel 广播给缩略图/面包屑等），
    // 并等缩略图管线排空后再继续键盘浏览。
    dirModel->setCurrentDirectory(dirPath);
    pump(100);
    CHECK(dirModel->currentDirectory() == dirPath, "open directory lands in DirectoryModel");
    pump(400); // 5 张 32x32 缩略图解码窗口

    // 键盘浏览：→ → ← （评审"滚轮切换"的键盘等价路径 navigate()）。
    sendKey(&w, Qt::Key_Right);
    CHECK(sel->currentImage() == paths[1], "Right arrow advances to image #2");
    sendKey(&w, Qt::Key_Right);
    CHECK(sel->currentImage() == paths[2], "Right arrow advances to image #3");
    sendKey(&w, Qt::Key_Left);
    CHECK(sel->currentImage() == paths[1], "Left arrow goes back to image #2");

    // Home / End / PageUp / PageDown 边界（beta_checklist "浏览体验"）。
    sendKey(&w, Qt::Key_End);
    CHECK(sel->currentImage() == paths.last(), "End jumps to the last image");
    sendKey(&w, Qt::Key_Home);
    CHECK(sel->currentImage() == paths.first(), "Home jumps to the first image");
    sendKey(&w, Qt::Key_PageUp); // 已在第一张：必须不越界、不崩溃
    CHECK(sel->currentImage() == paths.first(), "PageUp at first image does not underflow");
    sendKey(&w, Qt::Key_PageDown);
    CHECK(paths.contains(sel->currentImage()), "PageDown stays within the folder");

    // 快速切图后视图不能卡在空白/旧帧（评审"快速切图"稳定性检查）。
    if (viewer)
    {
        QElapsedTimer t;
        t.start();
        while (!viewer->frame() && t.elapsed() < 5000)
            pump(25);
        CHECK(viewer->frame() != nullptr,
              "after rapid keyboard navigation the viewer still shows a frame");
    }

    // 缩放循环：Fit → 100% → 放大 → Fit（评审"双击放大 → 恢复"的核心状态机）。
    if (viewer)
    {
        viewer->resize(800, 600);
        pump(30);

        if (viewer->frame())
        {
            viewer->zoomFit();
            pump(20);
            const double fitScale = viewer->viewTransform().scale;
            CHECK(fitScale > 0.0, "Fit produces a positive scale");

            viewer->zoomActual();
            pump(20);
            CHECK(qAbs(viewer->viewTransform().scale - 1.0) < 1e-9,
                  "100% (zoomActual) sets scale to exactly 1.0");

            viewer->zoomIn();
            pump(20);
            CHECK(viewer->viewTransform().scale > 1.0, "zoomIn grows the scale beyond 100%");

            viewer->zoomFit();
            pump(20);
            CHECK(qAbs(viewer->viewTransform().scale - fitScale) < 1e-6,
                  "restore (Fit) returns to the original fit scale");

            // Wheel zoom keeps the image point under the cursor stationary
            // (beta checklist “缩放中心：以鼠标点为中心”).
            {
                viewer->zoomFit();
                pump(20);
                const auto before = viewer->viewTransform();
                const QPointF cursor(220.0, 160.0);
                const double imgX = (cursor.x() - before.offsetX) / before.scale;
                const double imgY = (cursor.y() - before.offsetY) / before.scale;
                QWheelEvent wheel(cursor, viewer->mapToGlobal(cursor.toPoint()), QPoint(0, 0),
                                  QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                                  false);
                QApplication::sendEvent(viewer, &wheel);
                pump(20);
                const auto after = viewer->viewTransform();
                CHECK(after.scale > before.scale, "wheel zoom-in grows the scale");
                const double drift = qAbs((cursor.x() - after.offsetX) / after.scale - imgX) +
                                     qAbs((cursor.y() - after.offsetY) / after.scale - imgY);
                CHECK(drift < 0.5, "wheel zoom keeps the image point under the cursor fixed");
            }

            // Ctrl+C copies the current image to the clipboard. It must NEVER
            // open the file-copy dialog (the context menu no longer advertises
            // Ctrl+C for that action — same key, different behavior).
            QApplication::clipboard()->clear();
            sendKey(&w, Qt::Key_C, Qt::ControlModifier);
            pump(20);
            CHECK(!QApplication::clipboard()->image().isNull(),
                  "Ctrl+C copies the current image to the clipboard");

            // Slideshow (S): starts a timer that advances through the folder;
            // S again stops it. Settings drive the interval (min 500 ms).
            {
                QSettings slideshowSettings;
                slideshowSettings.setValue("slideshowInterval", 500);
                const QString beforeSlideshow = sel->currentImage();
                sendKey(&w, Qt::Key_S);
                pump(1200);
                const QString afterAdvance = sel->currentImage();
                CHECK(!afterAdvance.isEmpty() && afterAdvance != beforeSlideshow,
                      "S starts slideshow and advances to the next image");
                sendKey(&w, Qt::Key_S);
                pump(700);
                CHECK(sel->currentImage() == afterAdvance,
                      "S again stops slideshow (no further advance)");
            }

            // Double-click toggles fit <-> 100% at the cursor (beta
            // checklist "双击放大 -> 恢复").
            {
                viewer->zoomFit();
                pump(20);
                const double fitScale = viewer->viewTransform().scale;
                const QPointF dcPos(300.0, 200.0);
                QMouseEvent dbl(QEvent::MouseButtonDblClick, dcPos,
                                viewer->mapToGlobal(dcPos.toPoint()), Qt::LeftButton,
                                Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(viewer, &dbl);
                pump(20);
                CHECK(qAbs(viewer->viewTransform().scale - 1.0) < 1e-9,
                      "double-click zooms to 100%");
                QApplication::sendEvent(viewer, &dbl);
                pump(20);
                CHECK(qAbs(viewer->viewTransform().scale - fitScale) < 1e-6,
                      "second double-click restores Fit");
            }

            // Mouse side buttons navigate prev/next (cheat sheet
            // "← / → / 鼠标侧键").
            {
                const QString beforeNav = sel->currentImage();
                QMouseEvent back(QEvent::MouseButtonPress, QPointF(10, 10),
                                 viewer->mapToGlobal(QPoint(10, 10)), Qt::BackButton,
                                 Qt::BackButton, Qt::NoModifier);
                QApplication::sendEvent(viewer, &back);
                pump(100);
                CHECK(!sel->currentImage().isEmpty() && sel->currentImage() != beforeNav,
                      "mouse back button navigates to the previous image");
                QMouseEvent fwd(QEvent::MouseButtonPress, QPointF(10, 10),
                                viewer->mapToGlobal(QPoint(10, 10)), Qt::ForwardButton,
                                Qt::ForwardButton, Qt::NoModifier);
                QApplication::sendEvent(viewer, &fwd);
                pump(100);
                CHECK(sel->currentImage() == beforeNav,
                      "mouse forward button navigates back to the original image");
            }

            // I toggles the metadata overlay for the current image; ESC
            // hides it (cheat sheet “I/M → 图片信息浮层，ESC 关闭”).
            {
                // The viewer (and its overlay) are a top-level window.
                MetadataOverlay *overlay = nullptr;
                for (QWidget *top : QApplication::topLevelWidgets())
                {
                    overlay = top->findChild<MetadataOverlay *>();
                    if (overlay)
                        break;
                }
                CHECK(overlay != nullptr, "metadata overlay exists");
                if (overlay && !overlay->isVisible())
                {
                    sendKey(&w, Qt::Key_I);
                    pump(50);
                    CHECK(overlay->isVisible(), "I shows the metadata overlay");
                }
                sendKey(&w, Qt::Key_Escape);
                pump(50);
                CHECK(overlay && !overlay->isVisible(), "ESC hides the metadata overlay");
            }

            // M-viewer P0-3 async metadata-overlay histogram contract: showing
            // the overlay schedules exactly ONE Analysis batch that arrives
            // asynchronously (never blocking the toggle), a current-image change
            // clears the old histogram IMMEDIATELY through the synchronous SSOT
            // signals, and a fresh histogram for the new dimensions replaces it.
            // A release-gated blocker pins the single Analysis worker so the
            // queued batch and the immediate clear are observable.
            {
                auto *toggleAction = w.findChild<QAction *>("toggleMetadataAction");
                MetadataOverlay *overlay =
                    viewer ? viewer->findChild<MetadataOverlay *>() : nullptr;
                HistogramWidget *hist = overlay ? overlay->findChild<HistogramWidget *>() : nullptr;
                CHECK(toggleAction != nullptr, "metadata toggle action is discoverable");
                CHECK(overlay != nullptr, "metadata overlay is discoverable on the viewer");
                CHECK(hist != nullptr, "metadata overlay owns a histogram widget");

                auto &sched = TaskScheduler::instance();
                const QString imgA = sel->currentImage();
                {
                    QElapsedTimer syncFrame;
                    syncFrame.start();
                    while ((!viewer->frame() ||
                            QString::fromStdString(viewer->frame()->metadata().filePath) != imgA) &&
                           syncFrame.elapsed() < 8000)
                        pump(25);
                }
                waitForAnalysisIdle();
                sched.setQueueMaxThreads(TaskScheduler::Priority::Analysis, 1);
                struct RestoreAnalysisThreads
                {
                    ~RestoreAnalysisThreads()
                    {
                        TaskScheduler::instance().setQueueMaxThreads(
                            TaskScheduler::Priority::Analysis,
                            std::max(1, QThread::idealThreadCount() / 2));
                    }
                } restoreAnalysis;

                if (toggleAction && overlay && hist)
                {
                    const int64_t srcPixels =
                        viewer->frame()
                            ? static_cast<int64_t>(viewer->frame()->width()) *
                                  viewer->frame()->height()
                            : 0;
                    {
                        AnalysisBlocker blocker;
                        {
                            QElapsedTimer t;
                            t.start();
                            while (sched.metrics(TaskScheduler::PoolType::AnalysisPool)
                                       .active_tasks < 1 &&
                                   t.elapsed() < 5000)
                                pump(10);
                        }
                        CHECK(sched.metrics(TaskScheduler::PoolType::AnalysisPool)
                                  .active_tasks >= 1,
                              "metadata gate: blocker occupies the single Analysis worker");

                        const uint64_t submittedBefore =
                            sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
                        {
                            QElapsedTimer prompt;
                            prompt.start();
                            toggleAction->trigger();
                            pump(20);
                            CHECK(prompt.elapsed() < 2000,
                                  "metadata gate: toggle returns without sync histogram compute");
                        }
                        const uint64_t submittedAfter =
                            sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
                        CHECK(submittedAfter == submittedBefore + 1,
                              "metadata gate: showing schedules exactly one Analysis batch");
                        CHECK(overlay->isVisible(),
                              "metadata gate: overlay is visible after the toggle");
                        CHECK(toggleAction->isChecked(),
                              "metadata gate: toggle action is checked with the overlay");
                        CHECK(hist->histogramCount() == 0,
                              "metadata gate: no histogram while the Analysis worker is blocked");
                    }

                    CHECK(sched.drain(TaskScheduler::PoolType::AnalysisPool,
                                      std::chrono::seconds(15)),
                          "metadata gate: Analysis pool drains after the blocker release");
                    {
                        QElapsedTimer t;
                        t.start();
                        while (hist->histogramCount() == 0 && t.elapsed() < 8000)
                            pump(25);
                    }
                    CHECK(hist->histogramCount() == 1,
                          "metadata gate: async histogram arrives for the current image");
                    const long srcTotal = hist->histogramTotal(0);
                    CHECK(srcTotal == srcPixels,
                          "metadata gate: histogram total equals the frame pixel count");

                    const QString target = (imgA == paths[4]) ? paths[0] : paths[4];
                    CHECK(target != imgA, "metadata gate: target image has different dimensions");

                    const uint64_t submittedBefore2 =
                        sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
                    sel->setCurrentImage(target);
                    CHECK(hist->histogramCount() == 0,
                          "metadata gate: image change clears the old histogram immediately");
                    CHECK(overlay->isVisible(),
                          "metadata gate: overlay stays visible across the image change");
                    {
                        QElapsedTimer t;
                        t.start();
                        while ((!viewer->frame() ||
                                QString::fromStdString(viewer->frame()->metadata().filePath) !=
                                    target) &&
                               t.elapsed() < 8000)
                            pump(25);
                    }
                    {
                        QElapsedTimer t;
                        t.start();
                        while (hist->histogramCount() == 0 && t.elapsed() < 8000)
                            pump(25);
                    }
                    const uint64_t submittedAfter2 =
                        sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
                    CHECK(submittedAfter2 == submittedBefore2 + 1,
                          "metadata gate: the new image schedules exactly one fresh batch");
                    const long targetTotal = hist->histogramTotal(0);
                    const int64_t targetPixels =
                        viewer->frame()
                            ? static_cast<int64_t>(viewer->frame()->width()) *
                                  viewer->frame()->height()
                            : 0;
                    CHECK(targetTotal == targetPixels && targetTotal != srcTotal,
                          "metadata gate: fresh histogram has the new dimensions");

                    sendKey(&w, Qt::Key_Escape);
                    pump(50);
                    CHECK(!overlay->isVisible(), "metadata gate: ESC hides the overlay");
                    sel->setCurrentImage(imgA);
                    {
                        QElapsedTimer t;
                        t.start();
                        while ((!viewer->frame() ||
                                QString::fromStdString(viewer->frame()->metadata().filePath) !=
                                    imgA) &&
                               t.elapsed() < 8000)
                            pump(25);
                    }
                    waitForAnalysisIdle();
                }
            }

            // F toggles fullscreen: the visible viewer, or the main window
            // when the viewer is hidden (cheat sheet “F / F11 → 全屏切换”).
            {
                if (viewer->isFullScreen())
                {
                    sendKey(&w, Qt::Key_F);
                    pump(20);
                }
                CHECK(!viewer->isFullScreen(), "viewer starts in normal state");
                sendKey(&w, Qt::Key_F);
                pump(50);
                CHECK(viewer->isFullScreen(), "F fullscreens the visible viewer");
                sendKey(&w, Qt::Key_F);
                pump(50);
                CHECK(!viewer->isFullScreen(), "F again exits fullscreen");

                viewer->hide();
                pump(20);
                sendKey(&w, Qt::Key_F);
                pump(50);
                CHECK(w.isFullScreen(), "F fullscreens the main window when the viewer is hidden");
                sendKey(&w, Qt::Key_F);
                pump(50);
                CHECK(!w.isFullScreen(), "F again exits the main-window fullscreen");
                viewer->show();
                pump(20);
            }
        }
        viewer->close();
        pump(20);
    }

    // Compare requires 2-8 images: a single-image folder must not open a
    // degenerate one-pane compare dialog (user gets feedback instead).
    {
        const QString singleDir = sourceDir.filePath(QStringLiteral("wf_single"));
        QDir().mkpath(singleDir);
        const QString singlePath =
            writePng(QDir(singleDir), QStringLiteral("only.png"), QColor(200, 100, 50));
        directoryTree->navigateTo(singleDir, true);
        pump(200);
        sel->setCurrentImage(singlePath);
        sendKey(&w, Qt::Key_C);
        pump(100);
        bool compareOpened = false;
        for (QWidget *top : QApplication::topLevelWidgets())
        {
            auto *dlg = qobject_cast<QDialog *>(top);
            if (dlg && dlg->isVisible() && dlg->windowTitle().contains("比较模式"))
            {
                compareOpened = true;
                break;
            }
        }
        CHECK(!compareOpened, "Compare does not open with fewer than two images");
        directoryTree->navigateTo(dirPath, true);
        pump(200);
    }

    // Global search end-to-end: query a filename fragment, get results,
    // and double-click a row to open the image (shipped feature, full chain).
    {
        if (searchAction)
            searchAction->trigger();
        pump(50);
        auto *searchPanelWidget = w.findChild<SearchPanel *>();
        auto *searchEdit =
            searchPanelWidget ? searchPanelWidget->findChild<QLineEdit *>() : nullptr;
        auto *resultTable =
            searchPanelWidget ? searchPanelWidget->findChild<QTableWidget *>() : nullptr;
        CHECK(searchPanelWidget != nullptr, "global search panel is discoverable");
        CHECK(searchEdit != nullptr, "search panel exposes a query input");
        CHECK(resultTable != nullptr, "search panel exposes a results table");
        if (searchEdit && resultTable)
        {
            searchEdit->setText(QStringLiteral("wf_001"));
            QElapsedTimer searchTimer;
            searchTimer.start();
            while (resultTable->rowCount() == 0 && searchTimer.elapsed() < 10000)
                pump(50);
            CHECK(resultTable->rowCount() >= 1, "search returns a result for a filename fragment");
            bool named = false;
            for (int r = 0; r < resultTable->rowCount(); ++r)
                if (resultTable->item(r, 1) && resultTable->item(r, 1)->text().contains("wf_001"))
                    named = true;
            CHECK(named, "search result names the matched file");
            if (resultTable->rowCount() > 0)
            {
                resultTable->doubleClicked(resultTable->model()->index(0, 0));
                pump(300);
                CHECK(sel->currentImage().contains("wf_001"),
                      "double-clicking a search result opens the image");
            }
            searchEdit->clear();
        }
        if (searchAction)
            searchAction->trigger(); // hide the panel again
        pump(50);
    }

    // M25: closing while the BROWSE WORKSPACE is active must behave like the
    // focus-mode case — persist the state from before Browse hid the panels,
    // not the temporary hidden Browse layout. Exercised FIRST (window open),
    // then the window is re-shown for the existing focus-mode regression.
    if (analysisAction)
    {
        analysisAction->setChecked(false);
        analysisAction->trigger();
    }
    if (searchAction)
    {
        searchAction->setChecked(false);
        searchAction->trigger();
    }
    pump(10);
    CHECK(analysisPanel && analysisPanel->isVisible() && searchPanel && searchPanel->isVisible(),
          "analysis + search panels are visible before the close-in-browse regression");
    if (browseWorkspaceAction)
        browseWorkspaceAction->trigger(); // enter Browse: panels hide temporarily
    pump(10);
    CHECK(browseWorkspaceAction && browseWorkspaceAction->isChecked() && analysisPanel &&
              !analysisPanel->isVisible(),
          "browse workspace hides the panels before close persistence is exercised");
    w.close();
    pump(50);
    const AppState persistedAfterBrowse = AppState::load();
    QSettings persistedSettings2;
    CHECK(persistedAfterBrowse.analysisVisible,
          "closing inside the Browse workspace persists the pre-Browse analysis visibility");
    CHECK(persistedSettings2.value("searchVisible", false).toBool(),
          "closing inside the Browse workspace persists the pre-Browse search visibility");
    w.show();
    pump(50);
    if (browseWorkspaceAction)
        browseWorkspaceAction->trigger(); // exit Browse: panels restored
    pump(10);

    // Closing while Focus Browse is active must persist the panel state from
    // before Focus hid the panels, not the temporary hidden state.
    if (analysisAction)
    {
        analysisAction->setChecked(false);
        analysisAction->trigger();
    }
    if (searchAction)
    {
        searchAction->setChecked(false);
        searchAction->trigger();
    }
    CHECK(analysisPanel && analysisPanel->isVisible() && searchPanel && searchPanel->isVisible(),
          "analysis panel is visible before the close-in-focus regression");
    if (focusAction)
        focusAction->trigger();
    pump(10);
    CHECK(focusAction && focusAction->isChecked() && analysisPanel && !analysisPanel->isVisible(),
          "focus mode hides the panel before close persistence is exercised");
    w.close();
    pump(50);
    const AppState persisted = AppState::load();
    QSettings persistedSettings;
    CHECK(persisted.analysisVisible,
          "closing in focus mode persists the pre-focus analysis visibility");
    CHECK(persistedSettings.value("searchVisible", false).toBool(),
          "closing in focus mode persists the pre-focus search visibility");
    QFile::remove(nextPath);
    QDir().rmdir(nextDir);
    pump(50);
    CHECK(true, "main window closes cleanly after the browse workflow");
}

void workflow3_session_restore(const QString &dirPath, const QString &imagePath)
{
    std::cout << "—— Workflow 3: session restore ——\n";
    const QString recoveryPath =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/recovery.json";
    QFile::remove(recoveryPath);
    QSettings().clear();

    AppState expected;
    expected.lastDir = dirPath;
    expected.lastImage = imagePath;
    expected.analysisVisible = true;
    CHECK(expected.save(), "cross-restart fixture saves Analysis visibility");
    QSettings settings;
    settings.setValue("searchVisible", true);
    // Reproduce an older persisted session: LargeIcon is a fixed 240px preset,
    // while the old slider value may still contain the previous 180px grid size.
    settings.setValue("thumbViewMode", static_cast<int>(ThumbnailPanel::LargeIcon));
    settings.setValue("thumbSize", 180);
    settings.sync();

    {
        MainWindow w;
        w.resize(1280, 800);
        w.show();
        pump(350);
        auto *analysisPanel = w.findChild<QWidget *>("analysisPanel");
        auto *searchPanel = w.findChild<QWidget *>("searchPanel");
        auto *analysisAction = w.findChild<QAction *>("toggleAnalysisPanelAction");
        auto *searchAction = w.findChild<QAction *>("toggleSearchPanelAction");
        auto *browseAction = w.findChild<QAction *>("browseWorkspaceAction");
        auto *thumbnailPanel = w.findChild<ThumbnailPanel *>();
        auto *thumbnailSizeSlider = w.findChild<QSlider *>("thumbnailSizeSlider");
        auto *viewModeCombo = w.findChild<QComboBox *>("thumbnailViewModeCombo");
        CHECK(analysisPanel && analysisPanel->isVisible() && analysisAction &&
                  analysisAction->isChecked(),
              "Analysis panel and action restore together across a new MainWindow");
        CHECK(searchPanel && searchPanel->isVisible() && searchAction && searchAction->isChecked(),
              "Search panel and action restore together across a new MainWindow");
        CHECK(browseAction && !browseAction->isChecked(),
              "restored side panels keep the Browser workspace action unchecked");
        CHECK(
            thumbnailPanel && thumbnailPanel->viewMode() == ThumbnailPanel::LargeIcon &&
                thumbnailPanel->thumbSize() == 240 && thumbnailSizeSlider &&
                thumbnailSizeSlider->value() == 240 && !thumbnailSizeSlider->isEnabled() &&
                viewModeCombo && viewModeCombo->currentData().toInt() == ThumbnailPanel::LargeIcon,
            "deferred restore keeps the Large preset at 240px with a disabled synchronized slider");
        w.close();
        pump(50);
    }

    QFile::remove(recoveryPath);
    QSettings().clear();
    const QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!configPath.isEmpty())
        QDir(configPath).removeRecursively();
}

// ─── Workflow 2: 两图 Compare → 模式切换 → 互斥 → 退出 → 继续浏览 ───────────
void workflow2_compare(const QString &pathA, const QString &pathB)
{
    std::cout << "── Workflow 2: compare ──\n";

    SelectionModel sel;

    // 真实退出路径：CompareWorkspace 宿主是 QDialog，Esc → reject()。
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    ws->setImages({pathA, pathB});
    dlg.resize(1100, 750);
    dlg.show();
    // M28 P1-01: compare loads are async; wait for both frames deterministically.
    {
        QElapsedTimer cmpLoad;
        cmpLoad.start();
        while (ws->comparedImageCount() != 2 && cmpLoad.elapsed() < 5000)
            pump(25);
    }

    QWidget *modeToolbar = ws->findChild<QWidget *>("compareModeToolbar");
    QWidget *viewToolbar = ws->findChild<QWidget *>("compareViewToolbar");
    QWidget *toolToolbar = ws->findChild<QWidget *>("compareToolToolbar");
    const int availableToolbarWidth = ws->contentsRect().width();
    CHECK(modeToolbar && viewToolbar && toolToolbar && modeToolbar->isVisible() &&
              viewToolbar->isVisible() && toolToolbar->isVisible(),
          "compare exposes three visible semantic toolbars");
    CHECK(modeToolbar && modeToolbar->width() <= availableToolbarWidth &&
              modeToolbar->sizeHint().width() <= availableToolbarWidth,
          "compare mode toolbar fits the standard 1100px workspace width");
    CHECK(viewToolbar && viewToolbar->width() <= availableToolbarWidth &&
              viewToolbar->sizeHint().width() <= availableToolbarWidth,
          "compare view toolbar fits the standard 1100px workspace width");
    CHECK(toolToolbar && toolToolbar->width() <= availableToolbarWidth &&
              toolToolbar->sizeHint().width() <= availableToolbarWidth,
          "compare tool toolbar fits the standard 1100px workspace width");

    // 默认状态（评审"默认值不合理"检查）。
    CHECK(ws->comparedImageCount() == 2, "two images loaded into Compare");
    CHECK(ws->isSyncEnabled(), "sync zoom/pan is ON by default");
    CHECK(sel.compared().size() == 2, "SelectionModel.compared mirrors the compare set (SSOT)");
    CHECK(!sel.focused().isEmpty(), "a default focus/reference image is set");

    QCheckBox *blink = findChk(ws, QStringLiteral("闪烁对比"));
    QCheckBox *split = findChk(ws, QStringLiteral("左右分割"));
    QCheckBox *swipe = findChk(ws, QStringLiteral("滑动对比"));
    QCheckBox *overlay = findChk(ws, QStringLiteral("叠加对比"));
    QCheckBox *checker = findChk(ws, QStringLiteral("棋盘"));
    CHECK(blink && split && overlay, "mode checkboxes exist (blink/split/overlay)");
    CHECK(blink && split && swipe && overlay && checker && blink->isVisible() &&
              split->isVisible() && swipe->isVisible() && overlay->isVisible() &&
              checker->isVisible(),
          "all key compare mode controls remain visible at 1100x750");
    if (!blink || !split || !overlay)
        return;
    CHECK(split->isEnabled() && overlay->isEnabled(),
          "2-image modes are enabled for exactly two images");

    // 闪烁（B）开 → 关。
    sendKey(ws, Qt::Key_B);
    CHECK(blink->isChecked(), "key B turns blink compare ON");
    sendKey(ws, Qt::Key_B);
    CHECK(!blink->isChecked(), "key B turns blink compare OFF again");

    // 左右分割（S）→ 叠加（O）互斥：切换叠加后分割必须自动关闭。
    sendKey(ws, Qt::Key_S);
    CHECK(split->isChecked(), "key S turns split compare ON");
    sendKey(ws, Qt::Key_O);
    CHECK(overlay->isChecked(), "key O turns overlay compare ON");
    CHECK(!split->isChecked(), "overlay auto-unchecks split (no stale mode state)");

    // 棋盘格（K）→ 叠加自动关闭。
    if (checker && checker->isEnabled())
    {
        sendKey(ws, Qt::Key_K);
        CHECK(checker->isChecked(), "key K turns checkerboard compare ON");
        CHECK(!overlay->isChecked(), "checkerboard auto-unchecks overlay");
        sendKey(ws, Qt::Key_K);
        CHECK(!checker->isChecked(), "key K turns checkerboard OFF");
    }
    else
    {
        // overlay 仍开着：关闭，回到网格。
        sendKey(ws, Qt::Key_O);
        CHECK(!overlay->isChecked(), "key O turns overlay OFF again");
    }
    CHECK(!split->isChecked() && !overlay->isChecked() && !blink->isChecked(),
          "all exclusive modes are OFF after the round-trip (grid restored)");

    // 差异高亮（H）独立开关，不干扰其他模式。
    QCheckBox *diffHl = findChk(ws, QStringLiteral("差异高亮"));
    if (diffHl)
    {
        sendKey(ws, Qt::Key_H);
        CHECK(diffHl->isChecked(), "key H turns diff-highlight ON");
        sendKey(ws, Qt::Key_H);
        CHECK(!diffHl->isChecked(), "key H turns diff-highlight OFF");
    }

    // Space 按住 = 临时闪烁；松开必须恢复（评审"状态不同步"高危点）。
    sendKeyPressOnly(ws, Qt::Key_Space);
    CHECK(blink->isChecked(), "holding Space starts temporary blink");
    sendKeyReleaseOnly(ws, Qt::Key_Space);
    CHECK(!blink->isChecked(), "releasing Space stops temporary blink (no stuck state)");

    // Compare 会话可快照（供退出→重进恢复；round-trip 细节由 compare_session_tests 覆盖）。
    const auto session = ws->compareSession();
    CHECK(session.imageIds.size() == 2, "compareSession snapshots both images before exit");

    RawImageView *referenceView = nullptr;
    for (RawImageView *view : ws->findChildren<RawImageView *>())
    {
        if (view && view->cellIndex() == 1 && view->isVisible() && view->isEnabled())
        {
            referenceView = view;
            break;
        }
    }
    CHECK(referenceView != nullptr, "compare exposes the second pane as a RawImageView");
    if (referenceView)
    {
        auto bundle = ws->buildReportBundle();
        if (bundle.referenceIndex != 1)
        {
            const QPoint point = referenceView->rect().center();
            QMouseEvent event(QEvent::MouseMove, QPointF(point), Qt::NoButton, Qt::NoButton,
                              Qt::NoModifier);
            QApplication::sendEvent(referenceView, &event);
            pump(20);

            QPushButton *lockReference = findBtn(ws, QStringLiteral("锁定基准"));
            CHECK(lockReference != nullptr, "compare exposes the lock-reference button");
            if (lockReference)
                lockReference->click();
            pump(20);
            bundle = ws->buildReportBundle();
        }

        CHECK(bundle.referenceIndex == 1 && bundle.targets.size() == 1,
              "report bundle locks the second pane and emits one target");
        CHECK(bundle.images.size() == 2 && bundle.adjustments.size() == 2,
              "report bundle retains both images and both adjustment states");
        CHECK(bundle.images[static_cast<size_t>(bundle.referenceIndex)] == pathB.toStdString() &&
                  bundle.targets[0].path == pathA.toStdString(),
              "report bundle maps the locked reference to path B and target to path A");
    }

    // ── Cache gate: Compare hover smoothness ──
    // Hover annotations (synced crosshair, ROI, focus border, link markers,
    // size-mismatch badge) must only repaint the live vector layer on top of a
    // viewport-bounded cached base surface — never re-scale the full source
    // image. Each pane reports how many times it actually re-rasterized that
    // surface through the diagnostic QObject property baseSurfaceRenderCount
    // (a dynamic property, not a public API change).
    {
        RawImageView *pane0 = nullptr;
        RawImageView *pane1 = nullptr;
        for (RawImageView *v : ws->findChildren<RawImageView *>())
        {
            if (!v || !v->isVisible() || !v->isEnabled())
                continue;
            if (v->cellIndex() == 0)
                pane0 = v;
            else if (v->cellIndex() == 1)
                pane1 = v;
        }
        CHECK(pane0 != nullptr && pane1 != nullptr,
              "cache gate: normal grid exposes both RawImageView panes");
        if (!pane0 || !pane1)
            return;
        auto renderCount = [](RawImageView *v)
        {
            return v->property("baseSurfaceRenderCount").toULongLong();
        };
        {
            QElapsedTimer t;
            t.start();
            while ((pane0->image().isNull() || pane1->image().isNull()) && t.elapsed() < 5000)
                pump(25);
        }
        pump(50);
        const quint64 first0 = renderCount(pane0);
        const quint64 first1 = renderCount(pane1);
        CHECK(first0 > 0 && first1 > 0,
              "cache gate: each pane rasterized its base surface on first paint");

        // Enable the real synced-crosshair control, then drive real hover moves
        // that are each processed through separate event-loop turns. Every move
        // repaints the live crosshair; none may re-rasterize the source image.
        quint64 before0 = first0;
        quint64 before1 = first1;
        QCheckBox *crosshairChk = findChk(ws, QStringLiteral("同步准星"));
        CHECK(crosshairChk != nullptr, "cache gate: synced-crosshair control is discoverable");
        if (crosshairChk)
        {
            crosshairChk->setChecked(true);
            pump(50);
            before0 = renderCount(pane0);
            before1 = renderCount(pane1);
            const QPoint a(pane0->width() / 2 - 5, pane0->height() / 2 - 5);
            const QPoint b(pane0->width() / 2 + 5, pane0->height() / 2 + 5);
            for (int i = 0; i < 4; ++i)
            {
                // Each synthetic move is processed through its own event-loop
                // turn (pump after every move, not once after all moves) so the
                // repaint it triggers is delivered before the next move — no
                // repaint coalescing can hide a re-rasterization in between.
                sendMouseMove(pane0, a);
                pump(25);
                sendMouseMove(pane0, b);
                pump(25);
            }
            CHECK(pane0->hasCrosshair() && pane1->hasCrosshair(),
                  "cache gate: synced crosshair is live on both panes");
            CHECK(renderCount(pane0) == before0 && renderCount(pane1) == before1,
                  "cache gate: crosshair repaints reuse the cached base surface");
            crosshairChk->setChecked(false);
            pump(30);
        }

        // An authoritative transform change rebuilds every affected pane
        // surface; a repeated identical push invalidates and repaints nothing.
        ws->engine().setScale(2.0);
        ws->engine().setOffset(19.0, 23.0);
        ws->update();
        pump(60);
        const quint64 rebuilt0 = renderCount(pane0);
        const quint64 rebuilt1 = renderCount(pane1);
        CHECK(rebuilt0 > before0 && rebuilt1 > before1,
              "cache gate: authoritative scale/offset change rebuilds the pane surfaces");
        CHECK(qAbs(pane0->scale() - 2.0) < 1e-9 && qAbs(pane1->scale() - 2.0) < 1e-9,
              "cache gate: the authoritative scale reaches both panes");

        ws->update();
        pump(60);
        CHECK(renderCount(pane0) == rebuilt0 && renderCount(pane1) == rebuilt1,
              "cache gate: repeated identical transform push neither invalidates nor repaints");
    }

    // ── Wheel-zoom anchor regression ─────────────────────────────────────────
    // CompareWorkspace anchors wheel zoom in CENTER-RELATIVE coordinates
    // (RawImageView interprets offset as a pan delta from the widget center),
    // so the wheel position must be converted to center-relative before
    // applying the transform. Off-center wheel zoom must keep the same
    // image-space point under the cursor in the synchronized, the independent,
    // and both per-axis mixed modes, and a limit hit must derive the offset
    // from the effective (clamped) factor instead of re-scaling after the
    // offset was already computed.
    {
        RawImageView *pane0 = nullptr;
        RawImageView *pane1 = nullptr;
        for (RawImageView *v : ws->findChildren<RawImageView *>())
        {
            if (!v || !v->isVisible() || !v->isEnabled())
                continue;
            if (v->cellIndex() == 0)
                pane0 = v;
            else if (v->cellIndex() == 1)
                pane1 = v;
        }
        CHECK(pane0 != nullptr && pane1 != nullptr,
              "wheel gate: normal grid exposes both RawImageView panes");
        if (pane0 && pane1)
        {
            // Deterministic 40x transform: the 32x32 source (1280px) overflows
            // every pane dimension, so any cursor position maps inside the image
            // and the anchor math is not perturbed by offset clamping.
            const double kScale = 40.0;
            ws->engine().setScale(kScale);
            ws->engine().setOffset(12.0, -7.0);
            ws->update();
            pump(60);
            const QPoint cursor(pane0->width() * 3 / 4, pane0->height() / 4);
            auto wheelDrift = [pane0, cursor](int dy)
            {
                const QPointF before = pane0->widgetToImage(cursor);
                QWheelEvent ev(QPointF(cursor), pane0->mapToGlobal(cursor), QPoint(0, 0),
                               QPoint(0, dy), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                               false);
                QApplication::sendEvent(pane0, &ev);
                pump(30);
                const QPointF after = pane0->widgetToImage(cursor);
                return qAbs(after.x() - before.x()) + qAbs(after.y() - before.y());
            };

            // Synchronized path: one wheel must keep the image point under the
            // off-center cursor fixed and push the same transform to both panes.
            CHECK(wheelDrift(120) < 0.2,
                  "wheel gate: sync wheel zoom keeps the image point under the cursor");
            CHECK(qAbs(pane0->scale() - pane1->scale()) < 1e-9 &&
                      pane0->offset() == pane1->offset(),
                  "wheel gate: synchronized wheel zoom updates all panes uniformly");
            CHECK(qAbs(pane0->scale() - ws->engine().syncTransform().scale) < 1e-9,
                  "wheel gate: pane scale matches the engine sync scale after wheel");

            // Independent path: only the hovered pane changes, with the same
            // center-relative anchor semantics.
            ws->setSyncEnabled(false);
            pump(20);
            ws->engine().setCellScale(0, kScale);
            ws->engine().setCellOffset(0, 12.0, -7.0);
            ws->engine().setCellScale(1, kScale);
            ws->engine().setCellOffset(1, -9.0, 5.0);
            ws->update();
            pump(60);
            const double p1Scale = pane1->scale();
            const QPointF p1Off = pane1->offset();
            CHECK(wheelDrift(120) < 0.2,
                  "wheel gate: independent wheel zoom keeps the image point under the cursor");
            CHECK(pane0->scale() > kScale,
                  "wheel gate: independent wheel zoom grows the hovered pane");
            CHECK(qAbs(pane1->scale() - p1Scale) < 1e-9 && pane1->offset() == p1Off,
                  "wheel gate: independent wheel zoom leaves the other pane untouched");

            // Upper limit: a wheel past 50x must clamp to the ceiling and keep
            // the anchor (offset derived from the effective factor, not a post-zoom
            // clamp that would recompute the offset with the unclamped factor).
            ws->setSyncEnabled(true);
            pump(20);
            ws->engine().setScale(48.0);
            ws->engine().setOffset(12.0, -7.0);
            ws->update();
            pump(60);
            CHECK(wheelDrift(120) < 0.2,
                  "wheel gate: sync wheel zoom keeps the anchor at the 50x ceiling");
            CHECK(qAbs(pane0->scale() - 50.0) < 1e-9,
                  "wheel gate: sync wheel zoom clamps to the 50x ceiling");
            const QPointF limOff = pane0->offset();
            const quint64 ceilingRenders =
                pane0->property("baseSurfaceRenderCount").toULongLong();
            CHECK(wheelDrift(120) < 0.2,
                  "wheel gate: further wheel at the ceiling keeps the anchor");
            CHECK(qAbs(pane0->scale() - 50.0) < 1e-9 && pane0->offset() == limOff,
                  "wheel gate: scale stays at 50x with an identical transform");
            CHECK(pane0->property("baseSurfaceRenderCount").toULongLong() == ceilingRenders,
                  "wheel gate: ceiling no-op schedules no pane re-rasterization");

            // Zero wheel delta is ignored: no transform change, no repaint request.
            const double scaleBefore = pane0->scale();
            const QPointF offBefore = pane0->offset();
            const quint64 rendersBefore =
                pane0->property("baseSurfaceRenderCount").toULongLong();
            QWheelEvent zev(QPointF(cursor), pane0->mapToGlobal(cursor), QPoint(0, 0),
                            QPoint(0, 0), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(pane0, &zev);
            pump(30);
            CHECK(pane0->scale() == scaleBefore && pane0->offset() == offBefore,
                  "wheel gate: zero wheel delta produces no transform change");
            CHECK(pane0->property("baseSurfaceRenderCount").toULongLong() == rendersBefore,
                  "wheel gate: zero wheel delta produces no repaint request");

            // Per-axis mixed sync modes drive paintEvent's per-axis selection
            // (scale from the shared transform when zoom sync is on, offset from
            // the shared transform when drag sync is on), so the wheel must honor
            // each toggle independently through the real checkbox controls.
            QCheckBox *zoomSyncChk = findChk(ws, QStringLiteral("同步缩放"));
            QCheckBox *dragSyncChk = findChk(ws, QStringLiteral("同步拖动"));
            CHECK(zoomSyncChk != nullptr && dragSyncChk != nullptr,
                  "wheel gate: both per-axis sync controls must exist for the mixed-mode gate");
            if (zoomSyncChk && dragSyncChk)
            {
                // Zoom sync ON, drag sync OFF: every pane scales together through
                // the shared scale, but each pane keeps its own independently
                // zoomed offset.
                zoomSyncChk->setChecked(true);
                dragSyncChk->setChecked(false);
                pump(30);
                ws->engine().setScale(kScale);
                ws->engine().setOffset(12.0, -7.0);
                ws->engine().setCellOffset(0, 12.0, -7.0);
                ws->engine().setCellOffset(1, -9.0, 5.0);
                ws->update();
                pump(60);
                const double mixAScale = pane0->scale();
                const QPointF mixAOff0 = pane0->offset();
                const QPointF mixAOff1 = pane1->offset();
                CHECK(mixAOff0 != mixAOff1,
                      "wheel gate: mixed-A baseline keeps distinct per-pane offsets");
                CHECK(wheelDrift(120) < 0.2,
                      "wheel gate: mixed-A wheel keeps the image point under the cursor");
                CHECK(pane0->scale() > mixAScale && qAbs(pane0->scale() - pane1->scale()) < 1e-9,
                      "wheel gate: mixed-A wheel scales both panes together");
                CHECK(pane0->offset() != mixAOff0 && pane1->offset() != mixAOff1 &&
                          pane0->offset() != pane1->offset(),
                      "wheel gate: mixed-A wheel zooms each pane's own offset independently");

                // Zoom sync OFF, drag sync ON: only the hovered pane's scale
                // changes while both rendered offsets follow the shared
                // (drag-synced) offset.
                zoomSyncChk->setChecked(false);
                dragSyncChk->setChecked(true);
                pump(30);
                ws->engine().setScale(kScale);
                ws->engine().setOffset(12.0, -7.0);
                ws->engine().setCellScale(0, kScale);
                ws->engine().setCellScale(1, kScale);
                ws->update();
                pump(60);
                const double mixBScale1 = pane1->scale();
                const QPointF mixBOff = pane0->offset();
                CHECK(mixBOff == pane1->offset(),
                      "wheel gate: mixed-B baseline shares one offset across panes");
                CHECK(wheelDrift(120) < 0.2,
                      "wheel gate: mixed-B wheel keeps the image point under the cursor");
                CHECK(pane0->scale() > kScale,
                      "wheel gate: mixed-B wheel grows only the hovered pane");
                CHECK(qAbs(pane1->scale() - mixBScale1) < 1e-9,
                      "wheel gate: mixed-B wheel leaves the other pane's scale");
                CHECK(pane0->offset() == pane1->offset() && pane0->offset() != mixBOff,
                      "wheel gate: mixed-B wheel pans both panes through the shared offset");

                // Restore both sync toggles for the rest of the workflow.
                zoomSyncChk->setChecked(true);
                dragSyncChk->setChecked(true);
                pump(30);
            }
        }
    }

    // Esc 退出 Compare（真实 QDialog::reject 路径）。
    QSlider *thresholdSlider = ws->findChild<QSlider *>("diffThresholdSlider");
    QLabel *thresholdValueLabel = ws->findChild<QLabel *>("diffThresholdValueLabel");
    QSlider *brightnessSlider = ws->findChild<QSlider *>("brightnessSlider");
    QPushButton *resetButton = ws->findChild<QPushButton *>("resetAdjustmentsButton");
    QLabel *metricsLabel = ws->findChild<QLabel *>("diffMetricsLabel");
    QCheckBox *analysisToggle = ws->findChild<QCheckBox *>("analysisPanelToggle");
    QTableWidget *inspector = ws->findChild<QTableWidget *>("pixelInspectorTable");
    QWidget *histogram = ws->findChild<QWidget *>("analysisHistogram");
    CHECK(thresholdSlider && thresholdValueLabel && brightnessSlider && resetButton &&
              metricsLabel && analysisToggle && inspector && histogram,
          "compare analysis controls expose stable object names");
    if (thresholdSlider && thresholdValueLabel && brightnessSlider && resetButton && metricsLabel &&
        analysisToggle && inspector && referenceView)
    {
        analysisToggle->setChecked(true);
        pump(20);
        const QPoint paneCenter = referenceView->rect().center();
        sendMouseMove(referenceView, paneCenter);
        sendLeftClick(referenceView, paneCenter);
        CHECK(inspector->rowCount() == 2,
              "real pane click selects the edit target and populates the inspector");

        // CompareEngine is the viewport SSOT. Establish zoom/pan through the same
        // state path used by real wheel/drag input before testing image replacement.
        ws->engine().setScale(2.0);
        ws->engine().setOffset(19.0, 23.0);
        ws->update();
        pump(20);
        const double savedScale = referenceView->scale();
        const QPointF savedOffset = referenceView->offset();

        // Deterministic inspected coordinate: under the fixed transform (scale
        // 2.0, offset (19,23)) the widget point (width/2-12, height/2-8) maps to
        // image pixel (0,0) for any pane geometry, so the Inspector samples a
        // known pixel of the displayed pane image throughout this section.
        sendMouseMove(referenceView,
                      QPoint(referenceView->width() / 2 - 12, referenceView->height() / 2 - 8));

        // M29: diff overlays + metrics are computed asynchronously as ONE batch
        // per refresh request on the AnalysisPool. Every assertion below waits
        // for the delivery instead of assuming a synchronous refresh.
        waitForAnalysisIdle();
        const QString initialMetrics = metricsLabel->text();
        const QString initialInspector =
            inspector->item(1, 2) ? inspector->item(1, 2)->text() : QString();

        thresholdSlider->setValue(200);
        waitForAnalysisIdle();
        const QString thresholdMetrics = waitForMetricsChange(metricsLabel, initialMetrics);
        const QString thresholdInspector =
            inspector->item(1, 2) ? inspector->item(1, 2)->text() : QString();
        const auto thresholdBundle = ws->buildReportBundle();
        CHECK(ws->compareSession().threshold == 200 && thresholdBundle.threshold == 200,
              "threshold slider value reaches the compare report session");
        CHECK(thresholdValueLabel->text() == "200",
              "threshold value label follows the slider immediately");
        CHECK(thresholdMetrics != initialMetrics, "threshold change refreshes the report metrics");

        thresholdSlider->setSliderDown(true);
        thresholdSlider->setValue(0);
        pump(20);
        CHECK(thresholdValueLabel->text() == "0" && metricsLabel->text() == thresholdMetrics,
              "threshold drag defers expensive metrics refresh");
        thresholdSlider->setSliderDown(false);
        waitForMetricsText(metricsLabel, initialMetrics);
        CHECK(metricsLabel->text() == initialMetrics,
              "threshold release refreshes deferred metrics");
        thresholdSlider->setValue(200);
        waitForAnalysisIdle();

        // ── M29 gate: rapid threshold changes schedule ONE Analysis batch per
        // refresh request (never recompute inline on the UI thread), and only
        // the FINAL threshold may land on the overlay (latest-wins). A
        // release-gated blocker occupies the single Analysis worker so the two
        // queued batches are observed deterministically.
        {
            auto &sched = TaskScheduler::instance();
            sched.setQueueMaxThreads(TaskScheduler::Priority::Analysis, 1);
            struct RestoreAnalysisThreads
            {
                ~RestoreAnalysisThreads()
                {
                    TaskScheduler::instance().setQueueMaxThreads(
                        TaskScheduler::Priority::Analysis,
                        std::max(1, QThread::idealThreadCount() / 2));
                }
            } restoreAnalysis;

            // The reference pane was locked to pane 2 earlier, so pane 0 is the
            // target and pane 1 the base. Confirm the delivered overlay equals
            // the threshold-200 heatmap before gating.
            RawImageView *targetView = nullptr;
            for (RawImageView *v : ws->findChildren<RawImageView *>())
                if (v && v->cellIndex() == 0)
                    targetView = v;
            CHECK(targetView != nullptr, "gate test: target pane (0) is a RawImageView");
            if (targetView)
            {
                const int targetBase = 1;
                const uint8_t finalThreshold = 180;
                const ImageData basePx = ws->engine().imageAt(targetBase)->pixels();
                const ImageData tgtPx = ws->engine().imageAt(0)->pixels();
                const auto expectedHeat = [&](uint8_t t) {
                    return mvcore::toQImage(DifferenceEngine::heatMap(
                        DifferenceEngine::applyThreshold(
                            DifferenceEngine::differenceMap(tgtPx, basePx), t)));
                };
                const QImage expectedBaseline = expectedHeat(200);
                const QImage expectedFinal = expectedHeat(finalThreshold);
                waitForAnalysisIdle();
                CHECK(targetView->overlay() == expectedBaseline,
                      "gate test: baseline overlay matches the threshold-200 heatmap");

                // Release-gated blocker: predicate wait on a condition variable
                // (no busy 1ms sleep). The RAII guard releases on every path so
                // the single Analysis worker can never stay blocked.
                std::mutex gateMtx;
                std::condition_variable gateCv;
                bool gateReleased = false;
                auto blocker = sched.submit(
                    TaskScheduler::Priority::Analysis,
                    [&gateMtx, &gateCv, &gateReleased](const TaskScheduler::TaskContext &)
                    {
                        std::unique_lock<std::mutex> lk(gateMtx);
                        gateCv.wait(lk, [&gateReleased] { return gateReleased; });
                    });
                struct ReleaseGateGuard
                {
                    std::mutex &mtx;
                    std::condition_variable &cv;
                    bool &released;
                    ~ReleaseGateGuard()
                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        released = true;
                        cv.notify_all();
                    }
                } releaseGate{gateMtx, gateCv, gateReleased};
                {
                    QElapsedTimer t;
                    t.start();
                    while (sched.metrics(TaskScheduler::PoolType::AnalysisPool).active_tasks < 1 &&
                           t.elapsed() < 5000)
                        pump(10);
                }
                CHECK(sched.metrics(TaskScheduler::PoolType::AnalysisPool).active_tasks >= 1,
                      "gate test: release-gated blocker occupies the Analysis worker");

                const uint64_t submittedBefore =
                    sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
                thresholdSlider->setValue(120);
                thresholdSlider->setValue(finalThreshold);
                pump(20);
                const uint64_t submittedAfter =
                    sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
                CHECK(submittedAfter == submittedBefore + 2,
                      "gate test: each threshold change schedules exactly one Analysis "
                      "batch, not inline compute");
                CHECK(targetView->overlay() == expectedBaseline,
                      "gate test: overlay keeps the old state while the batch is queued");

                {
                    std::lock_guard<std::mutex> lk(gateMtx);
                    gateReleased = true;
                }
                gateCv.notify_all();
                CHECK(sched.drain(TaskScheduler::PoolType::AnalysisPool,
                                  std::chrono::seconds(15)),
                      "gate test: Analysis pool drains after the gate release");
                {
                    QElapsedTimer t;
                    t.start();
                    while (targetView->overlay() != expectedFinal && t.elapsed() < 5000)
                        pump(25);
                }
                CHECK(targetView->overlay() == expectedFinal,
                      "gate test: final threshold lands on the overlay (latest-wins)");
                CHECK(sched.metrics(TaskScheduler::PoolType::AnalysisPool).pending == 0,
                      "gate test: Analysis scheduler converges after rapid changes");
                CHECK(sched.graphMetrics().handles == 0,
                      "gate test: no live scheduler handles after rapid changes");

                // Restore the 200 threshold baseline for the sections below.
                thresholdSlider->setValue(200);
                waitForAnalysisIdle();
            }
        }

        brightnessSlider->setValue(40);
        waitForAnalysisIdle();
        const QString adjustedMetrics = waitForMetricsChange(metricsLabel, thresholdMetrics);
        const QString adjustedInspector =
            inspector->item(1, 2) ? inspector->item(1, 2)->text() : QString();
        const auto adjustedBundle = ws->buildReportBundle();
        CHECK(brightnessSlider->value() == 40 && adjustedMetrics != thresholdMetrics &&
                  adjustedInspector != thresholdInspector && adjustedInspector != initialInspector,
              "brightness changes the selected pane metrics and Inspector RGB");
        CHECK(adjustedBundle.adjustments.size() == 2 &&
                  adjustedBundle.adjustments[0].brightness == 0 &&
                  adjustedBundle.adjustments[1].brightness == 40,
              "pane click selects the second pane as the edit target");
        CHECK(qAbs(referenceView->scale() - savedScale) < 1e-9 &&
                  referenceView->offset() == savedOffset,
              "brightness adjustment preserves the pane transform");

        // ── Async latest-wins display gate ─────────────────────────────────────
        // Contract: each live brightness adjustment schedules ONE cancellable
        // AnalysisPool display batch; the old pane image stays until delivery,
        // stale generations never land, committed metrics stay deferred while the
        // slider is held down, and the Pixel Inspector refreshes when the newest
        // display lands. Return to identity so the gated drag applies exactly two
        // fresh generations (40 then 80).
        brightnessSlider->setValue(0);
        pump(20);
        waitForMetricsText(metricsLabel, thresholdMetrics);
        const QString deferredMetrics = metricsLabel->text();
        {
            auto &sched = TaskScheduler::instance();
            sched.setQueueMaxThreads(TaskScheduler::Priority::Analysis, 1);
            struct RestoreAnalysisThreads
            {
                ~RestoreAnalysisThreads()
                {
                    TaskScheduler::instance().setQueueMaxThreads(
                        TaskScheduler::Priority::Analysis,
                        std::max(1, QThread::idealThreadCount() / 2));
                }
            } restoreAnalysis;

            // Expected displays from the same production helpers the pane uses: a
            // brightness-only adjustment is exactly adjustBrightness + toQImage.
            const ImageData basePx = ws->engine().imageAt(1)->pixels();
            const QImage expectedBright40 = mvcore::toQImage(adjustBrightness(basePx, 40));
            const QImage expectedBright80 = mvcore::toQImage(adjustBrightness(basePx, 80));

            waitForAnalysisIdle();
            const QImage oldPaneImage = referenceView->image();

            // Release-gated blocker occupies the single Analysis worker so the
            // two display batches are observed queued, never delivered.
            std::mutex gateMtx;
            std::condition_variable gateCv;
            bool gateReleased = false;
            auto blocker = sched.submit(
                TaskScheduler::Priority::Analysis,
                [&gateMtx, &gateCv, &gateReleased](const TaskScheduler::TaskContext &)
                {
                    std::unique_lock<std::mutex> lk(gateMtx);
                    gateCv.wait(lk, [&gateReleased] { return gateReleased; });
                });
            struct ReleaseGateGuard
            {
                std::mutex &mtx;
                std::condition_variable &cv;
                bool &released;
                ~ReleaseGateGuard()
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    released = true;
                    cv.notify_all();
                }
            } releaseGate{gateMtx, gateCv, gateReleased};
            {
                QElapsedTimer t;
                t.start();
                while (sched.metrics(TaskScheduler::PoolType::AnalysisPool).active_tasks < 1 &&
                       t.elapsed() < 5000)
                    pump(10);
            }
            CHECK(sched.metrics(TaskScheduler::PoolType::AnalysisPool).active_tasks >= 1,
                  "display gate: release-gated blocker occupies the Analysis worker");

            const uint64_t submittedBefore =
                sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
            brightnessSlider->setSliderDown(true);
            brightnessSlider->setValue(40);
            brightnessSlider->setValue(80);
            pump(20);
            const uint64_t submittedAfter =
                sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
            CHECK(submittedAfter == submittedBefore + 2,
                  "display gate: each live brightness adjustment schedules exactly one "
                  "Analysis display batch");
            CHECK(referenceView->image() == oldPaneImage,
                  "display gate: old pane image remains until the display batch delivers");
            CHECK(metricsLabel->text() == deferredMetrics,
                  "display gate: committed metrics stay deferred while the slider is held down");

            {
                std::lock_guard<std::mutex> lk(gateMtx);
                gateReleased = true;
            }
            gateCv.notify_all();
            CHECK(sched.drain(TaskScheduler::PoolType::AnalysisPool,
                              std::chrono::seconds(15)),
                  "display gate: Analysis pool drains after the gate release");
            {
                QElapsedTimer t;
                t.start();
                while (referenceView->image() != expectedBright80 && t.elapsed() < 5000)
                    pump(25);
            }
            CHECK(referenceView->image() == expectedBright80,
                  "display gate: final brightness-80 display lands (latest-wins)");
            CHECK(referenceView->image() != expectedBright40,
                  "display gate: stale brightness-40 display never lands");
            CHECK(metricsLabel->text() == deferredMetrics,
                  "display gate: metrics stay deferred while the slider is still held down");
            {
                const QString expectedR = QString::number(qRed(expectedBright80.pixel(0, 0)));
                QElapsedTimer t;
                t.start();
                while ((inspector->item(1, 2) ? inspector->item(1, 2)->text() : QString()) !=
                           expectedR &&
                       t.elapsed() < 5000)
                    pump(25);
            }
            CHECK((inspector->item(1, 2) ? inspector->item(1, 2)->text() : QString()) ==
                      QString::number(qRed(expectedBright80.pixel(0, 0))),
                  "display gate: Pixel Inspector refreshes to the new displayed pixel value");
            CHECK(sched.metrics(TaskScheduler::PoolType::AnalysisPool).pending == 0,
                  "display gate: Analysis scheduler converges after rapid adjustments");
            CHECK(sched.graphMetrics().handles == 0,
                  "display gate: no live scheduler handles after rapid adjustments");
        }
        brightnessSlider->setSliderDown(false);
        waitForMetricsChange(metricsLabel, deferredMetrics);
        CHECK(metricsLabel->text() != deferredMetrics,
              "adjustment release refreshes deferred metrics");

        resetButton->click();
        waitForAnalysisIdle();
        const QString resetInspector =
            inspector->item(1, 2) ? inspector->item(1, 2)->text() : QString();
        waitForMetricsText(metricsLabel, thresholdMetrics);
        CHECK(metricsLabel->text() == thresholdMetrics && resetInspector == thresholdInspector,
              "reset exactly restores the threshold-baseline metrics and Inspector RGB");
        CHECK(qAbs(referenceView->scale() - savedScale) < 1e-9 &&
                  referenceView->offset() == savedOffset,
              "reset preserves the pane transform");
    }

    sendKey(ws, Qt::Key_Escape);
    CHECK(!dlg.isVisible(), "Esc closes the Compare dialog");

    // 退出后继续浏览：SSOT 仍然可用且信号正常。
    sel.setCurrentImage(pathA);
    CHECK(sel.currentImage() == pathA, "browsing continues after leaving Compare");
}

// ─── Workflow 10: Compare canvas page visibility + interaction ───────────────
// The planned Compare refactor exposes two stable, discoverable widgets:
//   - "compareCanvas":  the interactive page owning wheel/drag while a canvas
//     compare mode (split / swipe / overlay / checkerboard) is active.
//   - "compareGridPage": the normal grid page, hidden in canvas modes.
// Hidden RawImageViews cannot receive wheel/drag, so in every canvas mode the
// canvas must be the visible+enabled page. Wheel zoom anchors CENTER-RELATIVE
// to the half-pane under the cursor (split/swipe) or the canvas (overlay);
// left-drag pans the shared offset by the mouse delta; switching canvas modes
// preserves that shared zoom/pan on the same canvas. Red-fails on current code
// because "compareCanvas" does not exist yet.
void workflow10_compare_canvas(const QString &pathA, const QString &pathB)
{
    std::cout << "── Workflow 10: compare canvas visibility + interaction ──\n";

    // Same synthetic two-image setup as Workflow 2.
    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    ws->setImages({pathA, pathB});
    dlg.resize(1100, 750);
    dlg.show();
    {
        QElapsedTimer cmpLoad;
        cmpLoad.start();
        while (ws->comparedImageCount() != 2 && cmpLoad.elapsed() < 5000)
            pump(25);
    }
    CHECK(ws->comparedImageCount() == 2, "canvas workflow: two images loaded into Compare");

    QWidget *canvas = ws->findChild<QWidget *>("compareCanvas");
    QWidget *gridPage = ws->findChild<QWidget *>("compareGridPage");
    QCheckBox *split = findChk(ws, QStringLiteral("左右分割"));
    QCheckBox *swipe = findChk(ws, QStringLiteral("滑动对比"));
    QCheckBox *overlay = findChk(ws, QStringLiteral("叠加对比"));
    QCheckBox *checker = findChk(ws, QStringLiteral("棋盘对比"));
    CHECK(canvas != nullptr, "canvas workflow: compare exposes the compareCanvas widget");
    if (!canvas || !split || !swipe || !overlay || !checker)
        return; // planned canvas contract is absent — the check above is the red gate
    CHECK(gridPage != nullptr, "canvas workflow: compare exposes the compareGridPage widget");
    if (!gridPage)
        return;

    // Default state: the grid page owns the area, the canvas stays out of it.
    CHECK(gridPage->isVisible() && !canvas->isVisible(),
          "canvas workflow: the normal grid page is the visible page by default");

    // Split ON → the canvas becomes the visible interactive page.
    split->setChecked(true);
    pump(20);
    CHECK(split->isChecked() && canvas->isVisible() && canvas->isEnabled() && gridPage->isHidden(),
          "canvas workflow: split shows the canvas and hides the grid page");
    CHECK(canvas->width() > 0 && canvas->height() > 0,
          "canvas workflow: the canvas occupies a real layout area");

    // M34: deterministic split geometry contract — the halves must cover the
    // whole canvas with no right-edge or seam gap. This is pure geometry (the
    // exact helper drawSplitCompare paints with), so it does not depend on image
    // aspect ratio, Fit letterboxing, or async pane materialization.
    {
        const QRect cv = canvas->rect();
        const auto halves = CompareWorkspace::splitRects(cv);
        const QRect left = halves.first;
        const QRect right = halves.second;
        CHECK(!left.isEmpty() && !right.isEmpty(),
              "canvas workflow: split halves are non-empty for a real canvas");
        CHECK(left.left() == cv.left() && left.top() == cv.top() && left.bottom() == cv.bottom(),
              "canvas workflow: split left half matches the canvas on left/top/bottom");
        CHECK(right.right() == cv.right() && right.top() == cv.top() &&
                  right.bottom() == cv.bottom(),
              "canvas workflow: split right half matches the canvas on right/top/bottom");
        CHECK(left.right() + 1 == right.left(),
              "canvas workflow: split halves are adjacent at the shared boundary");
        CHECK(left.united(right) == cv,
              "canvas workflow: split halves united cover the whole canvas");
        CHECK(qAbs(left.width() - right.width()) <= 1,
              "canvas workflow: split halves differ in width by at most 1");
    }

    // Split OFF → the grid page returns.
    split->setChecked(false);
    pump(20);
    CHECK(!split->isChecked() && gridPage->isVisible() && !canvas->isVisible(),
          "canvas workflow: leaving split restores the grid page");

    // ── Cursor-anchored wheel zoom on the canvas ──
    // Split draws one half-pane per image; the shared offset is a pan delta
    // from the HALF-PANE center (not the full canvas center), so the wheel
    // anchor must be the cursor position in half-pane center-relative terms.
    split->setChecked(true);
    pump(20);
    const double baseScale = ws->engine().syncTransform().scale;
    const Vec2 baseOffset = ws->engine().syncTransform().offset;
    const int splitMidX = canvas->width() / 2;
    const QPoint cursor(canvas->width() / 6, canvas->height() / 4); // left half-pane
    const double anchorX = cursor.x() - splitMidX / 2.0;
    const double anchorY = cursor.y() - canvas->height() / 2.0;
    QWheelEvent wheel(QPointF(cursor), canvas->mapToGlobal(cursor), QPoint(0, 0), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(canvas, &wheel);
    pump(20);
    const double zoomedScale = ws->engine().syncTransform().scale;
    const Vec2 zoomedOffset = ws->engine().syncTransform().offset;
    CHECK(zoomedScale > baseScale, "canvas workflow: wheel on the canvas zooms in");
    {
        const double factor = zoomedScale / baseScale;
        const double expectX = anchorX - (anchorX - baseOffset.x) * factor;
        const double expectY = anchorY - (anchorY - baseOffset.y) * factor;
        CHECK(qAbs(zoomedOffset.x - expectX) < 0.5 && qAbs(zoomedOffset.y - expectY) < 0.5,
              "canvas workflow: canvas wheel keeps the image point under the cursor "
              "(center-relative anchor math)");
    }

    // ── Left-drag pans the shared offset by the mouse delta ──
    {
        const Vec2 offBefore = ws->engine().syncTransform().offset;
        const QPoint p0 = canvas->rect().center() - QPoint(40, 30);
        const QPoint p1 = canvas->rect().center() + QPoint(60, 20);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(p0), canvas->mapToGlobal(p0),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &press);
        QMouseEvent move(QEvent::MouseMove, QPointF(p1), canvas->mapToGlobal(p1), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(p1), canvas->mapToGlobal(p1),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &release);
        pump(20);
        const Vec2 offAfter = ws->engine().syncTransform().offset;
        CHECK(qAbs(offAfter.x - (offBefore.x + p1.x() - p0.x())) < 1e-6 &&
                  qAbs(offAfter.y - (offBefore.y + p1.y() - p0.y())) < 1e-6,
              "canvas workflow: left-drag on the canvas pans the shared offset by the mouse "
              "delta");
    }

    // ── Canvas modes preserve the shared zoom/pan on the SAME canvas ──
    {
        const double keptScale = ws->engine().syncTransform().scale;
        const Vec2 keptOffset = ws->engine().syncTransform().offset;

        overlay->setChecked(true);
        pump(20);
        CHECK(overlay->isChecked() && !split->isChecked(),
              "canvas workflow: overlay replaces split (mutually exclusive)");
        CHECK(canvas->isVisible() && gridPage->isHidden(),
              "canvas workflow: overlay keeps the canvas as the visible page");
        CHECK(qAbs(ws->engine().syncTransform().scale - keptScale) < 1e-9 &&
                  qAbs(ws->engine().syncTransform().offset.x - keptOffset.x) < 1e-9 &&
                  qAbs(ws->engine().syncTransform().offset.y - keptOffset.y) < 1e-9,
              "canvas workflow: overlay preserves the shared zoom/pan from split");

        checker->setChecked(true);
        pump(20);
        CHECK(checker->isChecked() && !overlay->isChecked(),
              "canvas workflow: checkerboard replaces overlay (mutually exclusive)");
        CHECK(canvas->isVisible() && gridPage->isHidden(),
              "canvas workflow: checkerboard keeps the canvas as the visible page");
        CHECK(qAbs(ws->engine().syncTransform().scale - keptScale) < 1e-9 &&
                  qAbs(ws->engine().syncTransform().offset.x - keptOffset.x) < 1e-9 &&
                  qAbs(ws->engine().syncTransform().offset.y - keptOffset.y) < 1e-9,
              "canvas workflow: checkerboard preserves the shared zoom/pan across modes");

        swipe->setChecked(true);
        pump(20);
        CHECK(swipe->isChecked() && !checker->isChecked(),
              "canvas workflow: swipe replaces checkerboard (mutually exclusive)");
        CHECK(canvas->isVisible() && gridPage->isHidden(),
              "canvas workflow: swipe keeps the canvas as the visible page");
        CHECK(qAbs(ws->engine().syncTransform().scale - keptScale) < 1e-9 &&
                  qAbs(ws->engine().syncTransform().offset.x - keptOffset.x) < 1e-9 &&
                  qAbs(ws->engine().syncTransform().offset.y - keptOffset.y) < 1e-9,
              "canvas workflow: swipe preserves the shared zoom/pan across modes");

        // The SAME canvas stays the interactive page after the mode switches.
        // Cursor stays far from the swipe divider (default 50% x), so the wheel
        // zooms instead of being treated as divider drag.
        const double swipeScale = ws->engine().syncTransform().scale;
        const QPoint swipeCursor(canvas->width() / 4, canvas->height() * 3 / 4);
        QWheelEvent swipeWheel(QPointF(swipeCursor), canvas->mapToGlobal(swipeCursor), QPoint(0, 0),
                               QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                               false);
        QApplication::sendEvent(canvas, &swipeWheel);
        pump(20);
        CHECK(ws->engine().syncTransform().scale > swipeScale,
              "canvas workflow: wheel still zooms on the same canvas in swipe mode");
    }

    // Exit every canvas mode → the grid page returns.
    swipe->setChecked(false);
    pump(20);
    CHECK(!split->isChecked() && !overlay->isChecked() && !checker->isChecked() &&
              !swipe->isChecked(),
          "canvas workflow: all canvas compare modes are off");
    CHECK(gridPage->isVisible() && !canvas->isVisible(),
          "canvas workflow: leaving canvas modes restores the grid page");
}

// ─── Workflow 4: List 首屏只调度可见窗口，不能把全目录送入解码 ────────────────
void workflow4_list_scaling(const QString &rootDir)
{
    std::cout << "── Workflow 4: list scaling ──\n";
    const QString dirPath = QDir(rootDir).filePath(QStringLiteral("list_scaling"));
    QDir().mkpath(dirPath);
    QDir dir(dirPath);
    bool fixturesReady = true;
    for (int i = 0; i < 600; ++i)
    {
        QFile file(dir.filePath(QStringLiteral("empty_%1.png").arg(i, 3, 10, QChar('0'))));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            fixturesReady = false;
            break;
        }
    }
    CHECK(fixturesReady, "List scaling creates 600 empty PNG fixtures");

    auto &pipeline = ThumbnailPipeline::instance();
    pipeline.clear();
    const auto totalDecodeStarts = std::make_shared<std::atomic<int>>(0);
    const auto decodePaths = std::make_shared<std::unordered_set<std::string>>();
    const auto decodePathsMutex = std::make_shared<std::mutex>();
    {
        ThumbnailPanel panel;
        panel.setViewMode(ThumbnailPanel::List);
        panel.resize(1200, 800);
        panel.show();
        pump(30);
        pipeline.setDecodeFn(
            [totalDecodeStarts, decodePaths, decodePathsMutex](const std::string &path, int)
            {
                totalDecodeStarts->fetch_add(1, std::memory_order_relaxed);
                const std::lock_guard<std::mutex> lock(*decodePathsMutex);
                decodePaths->insert(path);
                return ImageData{};
            });
        panel.setDirectory(dirPath);
        QElapsedTimer entriesTimer;
        entriesTimer.start();
        while (panel.entries().size() < 600 && entriesTimer.elapsed() < 5000)
            pump(25);
        pump(500);
        size_t uniqueDecodePaths = 0;
        {
            const std::lock_guard<std::mutex> lock(*decodePathsMutex);
            uniqueDecodePaths = decodePaths->size();
        }
        std::cout << "    List decode starts: "
                  << totalDecodeStarts->load(std::memory_order_relaxed) << " total, "
                  << uniqueDecodePaths << " unique paths\n";
        CHECK(panel.entries().size() == 600, "List scaling loads all 600 model entries");
        CHECK(uniqueDecodePaths < 400,
              "List first screen schedules a bounded window instead of all 600 entries");
        pipeline.clear();
    }
    pipeline.clear();
    pipeline.setDecodeFn([](const std::string &path, int size)
                         { return Decoder::decodeScaled(path, size); });
    pump(100);
    dir.removeRecursively();
}

// ─── Workflow 5: Export must use the directory currently shown in the dialog ───
void workflow5_export_current_output_directory(const QString &rootDir)
{
    std::cout << "── Workflow 5: export current output directory ──\n";
    const QString sourceDirPath = QDir(rootDir).filePath(QStringLiteral("export_source"));
    const QString outputDirPath = QDir(rootDir).filePath(QStringLiteral("export_output"));
    QDir().mkpath(sourceDirPath);
    QDir().mkpath(outputDirPath);

    QDir sourceDir(sourceDirPath);
    QDir outputDir(outputDirPath);
    const QString sourcePath =
        writePng(sourceDir, QStringLiteral("export_source.png"), QColor(80, 120, 180));
    const QString sourceReport = sourceDir.filePath(QStringLiteral("export_report.csv"));
    const QString outputReport = outputDir.filePath(QStringLiteral("export_report.csv"));
    QFile::remove(sourceReport);
    QFile::remove(outputReport);

    {
        ExportDialog dialog(QStringList{sourcePath});
        auto *dirEdit = dialog.findChild<QLineEdit *>(QStringLiteral("exportOutputDirectoryEdit"));
        auto *modeCombo = dialog.findChild<QComboBox *>(QStringLiteral("exportModeCombo"));
        CHECK(dirEdit && modeCombo, "export exposes stable output directory and mode controls");
        if (dirEdit && modeCombo)
        {
            dirEdit->setText(outputDir.absolutePath());
            const int csvIndex = modeCombo->findData(QStringLiteral("csv"));
            CHECK(csvIndex >= 0, "export offers CSV mode");
            if (csvIndex >= 0)
            {
                modeCombo->setCurrentIndex(csvIndex);
                CHECK(modeCombo->currentData().toString() == QStringLiteral("csv"),
                      "export CSV mode is selected");
                CHECK(QMetaObject::invokeMethod(&dialog, "onExportClicked", Qt::DirectConnection),
                      "export dispatches synchronously");
                const bool outputExists = QFileInfo::exists(outputReport);
                const bool sourceExists = QFileInfo::exists(sourceReport);
                CHECK(outputExists,
                      std::string("CSV report is written to the edited output directory: ") +
                          outputReport.toStdString());
                CHECK(!sourceExists,
                      std::string("CSV report is not written to the stale source directory: ") +
                          sourceReport.toStdString());
            }
        }
    }

    QFile::remove(sourceReport);
    QFile::remove(outputReport);
    sourceDir.removeRecursively();
    outputDir.removeRecursively();
}

// ─── Workflow 6: MetadataIndexer 双消费者（搜索重建 + Camera filter）────────
// 打开大目录后 MainWindow 在 500ms 后触发搜索重建（reindexSearch），同时
// ThumbnailPanel 的 Camera filter 也触发自己的元数据索引。两个真实消费者
// 共用 MetadataIndexer：任何一方的新请求都不得静默取消另一方。
// 回归点：面板索引在途时被 MainWindow 重建取消 → m_metaIndexing 永远 true →
// Camera filter 永不生效（永久 loading）。
void workflow6_metadata_dual_consumer(const QString &rootDir)
{
    std::cout << "── Workflow 6: metadata dual-consumer (search + camera filter) ──\n";
    const QString dirPath = QDir(rootDir).filePath(QStringLiteral("dual_meta"));
    QDir().mkpath(dirPath);
    QDir dir(dirPath);
    // 4000 fake DNGs: 2000 SONY + 2000 NIKON. Enough that the panel's metadata
    // index is still in flight when MainWindow's 500ms re-index fires.
    bool fixturesReady = true;
    for (int i = 0; i < 4000; ++i)
    {
        const std::string p =
            dir.filePath(QStringLiteral("d_%1.dng").arg(i, 4, 10, QChar('0'))).toStdString();
        if (!writeFakeDng(p, (i % 2 == 0) ? "SONY" : "NIKON",
                          QString("BODY%1").arg(i % 5).toStdString(),
                          QString("LENS%1").arg(i % 7).toStdString(),
                          static_cast<uint16_t>(100 * (1 + i % 20))))
        {
            fixturesReady = false;
            break;
        }
    }
    CHECK(fixturesReady, "dual-consumer fixture writes 4000 fake DNGs");

    // Clean session: no recovery prompt, no leftover session state.
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
                  "/recovery.json");

    MainWindow w;
    w.resize(1280, 800);
    w.show();
    pump(100);
    auto *thumbnailPanel = w.findChild<ThumbnailPanel *>();
    auto *directoryTree = w.findChild<DirectoryTree *>();
    CHECK(thumbnailPanel != nullptr, "MainWindow exposes the gallery panel");
    CHECK(directoryTree != nullptr, "MainWindow exposes the directory tree");
    if (!thumbnailPanel || !directoryTree)
    {
        dir.removeRecursively();
        return;
    }

    // Real SSOT path: the tree broadcasts directoryChanged -> panel scan +
    // MainWindow re-index scheduling (500ms debounce).
    directoryTree->navigateTo(dirPath, true);
    QElapsedTimer entriesTimer;
    entriesTimer.start();
    while (thumbnailPanel->entries().size() < 4000 && entriesTimer.elapsed() < 30000)
        pump(25);
    std::cout << "    gallery entries after scan: " << thumbnailPanel->entries().size() << " ("
              << entriesTimer.elapsed() << " ms)\n";
    CHECK(thumbnailPanel->entries().size() == 4000, "gallery shows all 4000 rows");

    // Panel filter first: the panel's metadata index must be in flight before
    // MainWindow's 500ms re-index timer fires (folder-load → statsChanged).
    thumbnailPanel->setCameraFilter(QStringLiteral("sony"));

    // Confirm the panel index has genuinely started (shared cache begins
    // filling) before the MainWindow re-index supersedes it.
    QElapsedTimer startTimer;
    startTimer.start();
    while (mviewer::core::MetadataIndexer::instance().size() == 0 && startTimer.elapsed() < 4000)
        pump(10);

    // Now wait out MainWindow's re-index window, then require the camera filter
    // to have applied (exactly the 2000 SONY rows).
    QElapsedTimer filterTimer;
    filterTimer.start();
    while (thumbnailPanel->pathList().size() != 2000 && filterTimer.elapsed() < 20000)
        pump(25);
    CHECK(thumbnailPanel->pathList().size() == 2000,
          "camera filter 'sony' applies while MainWindow re-index runs concurrently "
          "(panel index was NOT silently cancelled)");
    CHECK(mviewer::core::MetadataIndexer::instance().size() >= 2000,
          "shared metadata cache holds the indexed entries");

    w.close();
    pump(100);
    dir.removeRecursively();
}

// ─── Workflow 7: rapid navigation cancels stale preloads, promotes the ──────
// matching neighbor preload to a foreground decode
// Regression for the M29 cancellable viewer loads. Uses the REAL ImageViewer +
// TaskScheduler metrics, deterministic (no wall-clock-only waits): a
// release-gated blocker occupies the single Background (MetadataPool) worker;
// opening the middle image queues its two neighbor preloads behind the blocker.
// Navigating to the LAST image consumes the queued p2 neighbor preload and
// promotes it to the foreground decode — exactly one new DecodePool submission
// (a single foreground escalation, not a re-queue) — while soft-cancelling the
// stale pre_0 preload so its work never runs.
void workflow7_stale_preload_cancellation(const QString &rootDir)
{
    std::cout << "── Workflow 7: stale preload cancellation + matching-neighbor promotion ──\n";

    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();

    // Fresh folder: these paths have never been decoded anywhere in the suite.
    const QString dirPath = QDir(rootDir).filePath(QStringLiteral("wf_preload"));
    QDir().mkpath(dirPath);
    QDir dir(dirPath);
    const QString p0 = writePng(dir, QStringLiteral("pre_0.png"), QColor(210, 40, 40));
    const QString p1 = writePng(dir, QStringLiteral("pre_1.png"), QColor(40, 210, 40));
    const QString p2 = writePng(dir, QStringLiteral("pre_2.png"), QColor(40, 40, 210));

    // Single Background worker (main() already pins it to 1).
    sched.setQueueMaxThreads(TaskScheduler::Priority::Background, 1);

    MainWindow w;
    w.resize(1280, 800);
    w.show();
    pump(100);

    ImageViewer *viewer = nullptr;
    for (QWidget *top : QApplication::topLevelWidgets())
        if (auto *v = qobject_cast<ImageViewer *>(top))
            viewer = v;
    CHECK(viewer != nullptr, "preload test: real viewer window exists");
    if (!viewer)
    {
        w.close();
        dir.removeRecursively();
        return;
    }

    // Drain first so no prior workflow's or MainWindow-startup Background work
    // is in flight before the gate is closed.
    CHECK(sched.drain(TaskScheduler::PoolType::MetadataPool, std::chrono::seconds(15)),
          "preload test: Background pool drained before gating");

    // Occupy the single Background worker with a release-gated blocker.
    std::atomic<bool> gate{false};
    auto blocker = sched.submit(TaskScheduler::Priority::Background,
                                [&gate](const TaskScheduler::TaskContext &)
                                {
                                    while (!gate.load(std::memory_order_acquire))
                                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                });
    {
        QElapsedTimer t;
        t.start();
        while (sched.metrics(TaskScheduler::PoolType::MetadataPool).active_tasks < 1 &&
               t.elapsed() < 5000)
            pump(10);
    }
    CHECK(sched.metrics(TaskScheduler::PoolType::MetadataPool).active_tasks >= 1,
          "preload test: release-gated blocker occupies the Background worker");

    // Open the middle image through the real viewer. Its foreground decode runs
    // on DecodePool and delivers on the UI thread; the delivery schedules the
    // two neighbor preloads (pre_0, pre_2) at Background priority -> queued.
    viewer->setImage(p1);
    {
        QElapsedTimer t;
        t.start();
        while ((!viewer->frame() || viewer->frame()->metadata().filePath != p1.toStdString()) &&
               t.elapsed() < 5000)
            pump(25);
    }
    CHECK(viewer->frame() && viewer->frame()->metadata().filePath == p1.toStdString(),
          "preload test: middle image displayed");

    // The two neighbor preloads must be queued behind the blocker: pending stays
    // >= 3 (blocker + pre_0 + pre_2) and neither has run yet (no cache warm).
    {
        QElapsedTimer t;
        t.start();
        while (sched.metrics(TaskScheduler::PoolType::MetadataPool).pending < 3 &&
               t.elapsed() < 5000)
            pump(10);
    }
    const auto gated = sched.metrics(TaskScheduler::PoolType::MetadataPool);
    CHECK(gated.pending >= 3, "preload test: both neighbor preloads are queued behind the blocker");
    {
        ImageData probe;
        CHECK(!CacheManager::instance().getMemory(CacheLevel::FullImage,
                                                  repo.makeKey(p0.toStdString()), probe),
              "preload test: queued neighbor preloads have not run (cache not warmed)");
    }

    // Rapid navigation: open the LAST image while Background is still gated.
    // setImage must consume the queued p2 neighbor preload and promote it to
    // the foreground decode (one DecodePool escalation), while soft-cancelling
    // the stale pre_0 preload. Snapshot DecodePool submissions before the
    // navigation so the promotion costs exactly one submission, measured
    // deterministically (not by wall-clock timing).
    const uint64_t decodeSubmittedBefore =
        sched.metrics(TaskScheduler::PoolType::DecodePool).submitted;
    viewer->setImage(p2);
    {
        QElapsedTimer t;
        t.start();
        while ((!viewer->frame() || viewer->frame()->metadata().filePath != p2.toStdString()) &&
               t.elapsed() < 5000)
            pump(25);
    }
    CHECK(viewer->frame() && viewer->frame()->metadata().filePath == p2.toStdString(),
          "preload test: latest navigation target displayed");

    // p2 was delivered through the normal foreground onLoaded path (the promoted
    // preload is not a special display mode), so its frame must carry a
    // histogram even though p2 arrived as a Background preload first.
    CHECK(viewer->frame() && viewer->frame()->hasHistogram(),
          "preload test: promoted neighbor displays with a histogram (foreground contract)");
    CHECK(sched.metrics(TaskScheduler::PoolType::DecodePool).submitted == decodeSubmittedBefore + 1,
          "preload test: matching-neighbor promotion causes exactly one DecodePool submission");

    // Release the gate and drain: pre_0 was ONLY ever a stale queued preload —
    // if rapid navigation did not cancel it, its work would now warm the cache.
    gate.store(true, std::memory_order_release);
    CHECK(sched.drain(TaskScheduler::PoolType::MetadataPool, std::chrono::seconds(15)),
          "preload test: Background drains after the gate release");

    {
        ImageData probe;
        const bool staleCached = CacheManager::instance().getMemory(
            CacheLevel::FullImage, repo.makeKey(p0.toStdString()), probe);
        CHECK(!staleCached,
              "rapid navigation cancels stale neighbor preload work (never warms FullImage)");
    }
    const auto m = sched.metrics(TaskScheduler::PoolType::MetadataPool);
    CHECK(m.pending == 0 && m.active_tasks == 0 && m.queue_depth == 0,
          "preload test: Background scheduler converges after rapid navigation");
    const auto g = sched.graphMetrics();
    CHECK(g.handles == 0, "preload test: no live scheduler handles after rapid navigation");

    // Restore the default Background thread count and close.
    sched.setQueueMaxThreads(TaskScheduler::Priority::Background,
                             std::max(1, QThread::idealThreadCount() / 2));
    viewer->close();
    pump(50);
    w.close();
    pump(50);
    dir.removeRecursively();
}

// ─── Workflow 8: PreviewPanel serves a SCALED preview through the ──────────
// Thumbnail pool, never the Decode pool. A real >512 image keeps its source
// dimensions while the held preview pixmap is capped at a 512 max edge, and
// the scaled result lands in the Preview cache without ever warming the
// FullImage cache (regression: the preview used to run a full DecodePool
// loadAsync decode, duplicate ImageViewer decode, and retain a full QPixmap).
void workflow8_preview_scaled_load(const QString &rootDir)
{
    std::cout << "── Workflow 8: preview scaled/cancellable load ──\n";

    const QString dirPath = QDir(rootDir).filePath(QStringLiteral("wf_preview"));
    QDir().mkpath(dirPath);
    QDir dir(dirPath);
    const QString path = dir.filePath(QStringLiteral("preview_big.png"));
    {
        QImage big(1024, 768, QImage::Format_RGB32);
        for (int y = 0; y < 768; y += 8)
        {
            QRgb *row = reinterpret_cast<QRgb *>(big.scanLine(y));
            for (int x = 0; x < 1024; ++x)
                row[x] = qRgb((x * 255) / 1024, (y * 255) / 768, ((x + y) * 255) / 1792);
        }
        CHECK(big.save(path, "PNG"), "preview workflow writes a real 1024x768 PNG");
    }

    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();
    const std::string key = repo.makeKey(path.toStdString());

    CHECK(sched.drain(TaskScheduler::PoolType::ThumbnailPool, std::chrono::seconds(15)),
          "preview workflow: Thumbnail pool drained before measuring");
    CHECK(sched.drain(TaskScheduler::PoolType::DecodePool, std::chrono::seconds(15)),
          "preview workflow: Decode pool drained before measuring");
    const uint64_t thumbSubmittedBefore =
        sched.metrics(TaskScheduler::PoolType::ThumbnailPool).submitted;
    const uint64_t decodeSubmittedBefore =
        sched.metrics(TaskScheduler::PoolType::DecodePool).submitted;
    {
        ImageData probe;
        CHECK(!CacheManager::instance().getMemory(CacheLevel::FullImage, key, probe),
              "preview workflow: FullImage cache does not hold the key beforehand");
    }

    {
        PreviewPanel panel;
        panel.resize(320, 240);
        panel.setImage(path);
        QElapsedTimer t;
        t.start();
        while (!panel.hasImage() && t.elapsed() < 10000)
            pump(25);

        CHECK(panel.hasImage(), "preview workflow: scaled preview is delivered");
        CHECK(sched.metrics(TaskScheduler::PoolType::ThumbnailPool).submitted ==
                  thumbSubmittedBefore + 1,
              "preview workflow: exactly one Thumbnail task serves the preview");
        CHECK(sched.metrics(TaskScheduler::PoolType::DecodePool).submitted == decodeSubmittedBefore,
              "preview workflow: zero Decode tasks serve the preview");
        {
            ImageData fullProbe;
            CHECK(!CacheManager::instance().getMemory(CacheLevel::FullImage, key, fullProbe),
                  "preview workflow: FullImage cache is never touched");
            ImageData previewProbe;
            CHECK(CacheManager::instance().getMemory(
                      CacheLevel::Preview, PreviewPanel::previewCacheKey(path.toStdString()),
                      previewProbe),
                  "preview workflow: scaled result lands in the Preview cache");
        }
        CHECK(panel.sourceImageSize() == QSize(1024, 768),
              "preview workflow: source dimensions are preserved");
        const QSize previewSize = panel.previewPixelSize();
        CHECK(previewSize.width() > 0 && previewSize.height() > 0 &&
                  std::max(previewSize.width(), previewSize.height()) <= 512,
              "preview workflow: preview pixmap is capped at a 512 max edge");

        // Re-selecting the same image reuses the Preview cache: still exactly
        // one Thumbnail submission, zero Decode work, dimensions preserved.
        const uint64_t thumbBeforeSecond =
            sched.metrics(TaskScheduler::PoolType::ThumbnailPool).submitted;
        const uint64_t decodeBeforeSecond =
            sched.metrics(TaskScheduler::PoolType::DecodePool).submitted;
        panel.setImage(path);
        QElapsedTimer t2;
        t2.start();
        while (!panel.hasImage() && t2.elapsed() < 10000)
            pump(25);
        CHECK(panel.hasImage(), "preview workflow: re-selected preview is delivered");
        CHECK(sched.metrics(TaskScheduler::PoolType::ThumbnailPool).submitted ==
                  thumbBeforeSecond + 1,
              "preview workflow: re-select submits exactly one Thumbnail task");
        CHECK(sched.metrics(TaskScheduler::PoolType::DecodePool).submitted == decodeBeforeSecond,
              "preview workflow: re-select still does zero Decode work");
        CHECK(panel.sourceImageSize() == QSize(1024, 768),
              "preview workflow: re-select preserves source dimensions");
    }

    // Scheduler convergence after the second (cache-hit) selection: every
    // preview task — including the delivered one whose handle was released on
    // the UI thread — must drain to zero pending/active/queued work and leave
    // the dependency graph with no live handles before the fixture directory
    // is deleted.
    CHECK(sched.drain(TaskScheduler::PoolType::ThumbnailPool, std::chrono::seconds(15)),
          "preview workflow: Thumbnail pool drains after the cache re-select");
    {
        const TaskScheduler::PoolMetrics pm = sched.metrics(TaskScheduler::PoolType::ThumbnailPool);
        CHECK(pm.pending == 0, "preview workflow: Thumbnail pool has zero pending tasks");
        CHECK(pm.active_tasks == 0, "preview workflow: Thumbnail pool has zero active tasks");
        CHECK(pm.queue_depth == 0, "preview workflow: Thumbnail pool queue is empty");
        CHECK(sched.graphMetrics().handles == 0,
              "preview workflow: scheduler dependency graph has no live handles");
    }

    dir.removeRecursively();
}

// ─── Workflow 9: Pixel Inspector lifecycle ─────────────────────────────────
// Real ImageViewer + real pixelInfo signal over a real decoded ImageFrame.
// QtDecoder normalizes a grayscale PNG to RGB24 (equal channels); moving the
// mouse onto image pixel (3,4) must report valid=true with that exact gray
// value. Every transition away from a live sample — setImage(), Leave, close —
// must synchronously invalidate the inspector (valid=false, x/y=-1): the
// emission count must grow and the last sample must flip to invalid.
void workflow9_pixel_inspector_lifecycle(const QString &rootDir)
{
    std::cout << "── Workflow 9: pixel inspector lifecycle ──\n";

    const QString dirPath = QDir(rootDir).filePath(QStringLiteral("wf_pixel"));
    QDir().mkpath(dirPath);
    QDir dir(dirPath);
    const QString grayPath = dir.filePath(QStringLiteral("gray.png"));
    const QString otherPath = dir.filePath(QStringLiteral("other.png"));
    {
        QImage gray(8, 8, QImage::Format_Grayscale8);
        gray.fill(QColor(73, 73, 73)); // nontrivial gray, never black/white
        CHECK(gray.save(grayPath, "PNG"), "pixel workflow writes a grayscale PNG");
        QImage other(8, 8, QImage::Format_RGB32);
        other.fill(qRgb(200, 100, 50));
        CHECK(other.save(otherPath, "PNG"), "pixel workflow writes a second PNG");
    }

    ImageViewer viewer;
    viewer.resize(320, 240);
    viewer.show();
    pump(50);

    // Capture every pixelInfo emission before the first setImage.
    struct PixelProbe
    {
        int emissions = 0;
        int x = -1;
        int y = -1;
        int r = -1;
        int g = -1;
        int b = -1;
        int a = -1;
        int rawKind = -1;
        bool valid = false;
    };
    PixelProbe probe;
    QObject::connect(
        &viewer, &ImageViewer::pixelInfo, &viewer,
        [&probe](int x, int y, int r, int g, int b, int a, int, int, int, int rawKind, bool valid)
        {
            ++probe.emissions;
            probe.x = x;
            probe.y = y;
            probe.r = r;
            probe.g = g;
            probe.b = b;
            probe.a = a;
            probe.rawKind = rawKind;
            probe.valid = valid;
        });

    viewer.setImage(grayPath);
    {
        QElapsedTimer t;
        t.start();
        while ((!viewer.frame() || viewer.frame()->metadata().filePath != grayPath.toStdString()) &&
               t.elapsed() < 5000)
            pump(25);
    }
    std::shared_ptr<ImageFrame> decodedFrame = viewer.frame();
    CHECK(decodedFrame && decodedFrame->metadata().filePath == grayPath.toStdString(),
          "pixel workflow: grayscale image decodes");
    CHECK(decodedFrame && decodedFrame->width() == 8 && decodedFrame->height() == 8,
          "pixel workflow: decoded frame keeps the 8x8 dimensions");
    // QtDecoder converts the source to RGB888 and stores it as RGB24 ImageData
    // (see toImageData in QtDecoder.cpp), so a grayscale PNG yields equal
    // channels. Sample pixel (3,4) via samplePixel, which canonicalises the
    // format and reports invalid on null/truncated buffers instead of reading
    // out of bounds.
    CHECK(decodedFrame && decodedFrame->pixels().format == PixelFormat::RGB24,
          "pixel workflow: QtDecoder normalizes grayscale PNG to RGB24");
    const PixelRGBA decodedSample =
        decodedFrame ? samplePixel(decodedFrame->pixels(), 3, 4) : PixelRGBA{};
    CHECK(decodedSample.valid, "pixel workflow: decoded sample at (3,4) is readable");
    CHECK(decodedSample.r == decodedSample.g && decodedSample.g == decodedSample.b,
          "pixel workflow: decoded sample channels are equal (gray normalization)");
    const int expectedGray = decodedSample.r;
    CHECK(expectedGray > 0 && expectedGray < 255,
          "pixel workflow: decoded gray value is nontrivial (not black/white)");
    std::cout << "    decoded gray sample: " << expectedGray << "\n";

    // Deterministic transform: zoomActual gives exactly 100% with an integer
    // center offset (156,116) at 320x240. The pixel center (offset+(coord+0.5)
    // *scale) sits on a half-pixel; rounding it up would land on the pixel
    // boundary, so floor keeps the point inside pixel (3,4).
    viewer.zoomActual();
    pump(20);
    CHECK(qAbs(viewer.viewTransform().scale - 1.0) < 1e-9,
          "pixel workflow: zoomActual restores exactly 100%");
    auto screenForPixel = [&viewer](int px, int py)
    {
        const auto v = viewer.viewTransform();
        return QPoint(qFloor(v.offsetX + (px + 0.5) * v.scale),
                      qFloor(v.offsetY + (py + 0.5) * v.scale));
    };
    sendMouseMove(&viewer, screenForPixel(3, 4));
    CHECK(probe.emissions >= 1 && probe.valid && probe.x == 3 && probe.y == 4,
          "pixel workflow: mouse over image pixel (3,4) reports a valid sample");
    CHECK(probe.r == expectedGray && probe.g == expectedGray && probe.b == expectedGray,
          "pixel workflow: pixelInfo channels match the decoded gray sample");
    CHECK(probe.a == 255 && probe.rawKind == 0,
          "pixel workflow: RGB24 sample reports opaque alpha and 8-bit kind");

    // Switching images must synchronously invalidate the inspector: the new
    // load emits a cleared sample before the next decode runs, so a stale
    // pixel never lingers while the new image loads.
    const int beforeSwitch = probe.emissions;
    viewer.setImage(otherPath);
    CHECK(probe.emissions > beforeSwitch && !probe.valid,
          "pixel workflow: setImage synchronously invalidates the inspector");
    {
        QElapsedTimer t;
        t.start();
        while (
            (!viewer.frame() || viewer.frame()->metadata().filePath != otherPath.toStdString()) &&
            t.elapsed() < 5000)
            pump(25);
    }
    CHECK(viewer.frame() && viewer.frame()->metadata().filePath == otherPath.toStdString(),
          "pixel workflow: second image decodes");

    // Re-open the gray image and confirm the inspector recovers.
    viewer.setImage(grayPath);
    {
        QElapsedTimer t;
        t.start();
        while ((!viewer.frame() || viewer.frame()->metadata().filePath != grayPath.toStdString()) &&
               t.elapsed() < 5000)
            pump(25);
    }
    CHECK(viewer.frame() && viewer.frame()->metadata().filePath == grayPath.toStdString(),
          "pixel workflow: gray image is restored");
    viewer.zoomActual();
    pump(20);
    sendMouseMove(&viewer, screenForPixel(3, 4));
    CHECK(probe.valid && probe.x == 3 && probe.y == 4,
          "pixel workflow: inspector recovers after re-opening the image");

    // Cursor leaving the view must invalidate the inspector so the panel stops
    // showing the last hovered pixel.
    const int beforeLeave = probe.emissions;
    QEvent leaveEvent(QEvent::Leave);
    QApplication::sendEvent(&viewer, &leaveEvent);
    CHECK(probe.emissions > beforeLeave && !probe.valid,
          "pixel workflow: Leave invalidates the inspector");

    // Closing the viewer must invalidate it too: the last sample must not
    // outlive the window.
    sendMouseMove(&viewer, screenForPixel(3, 4));
    CHECK(probe.valid && probe.x == 3 && probe.y == 4,
          "pixel workflow: sample is valid again before close");
    const int beforeClose = probe.emissions;
    viewer.close();
    CHECK(probe.emissions > beforeClose && !probe.valid,
          "pixel workflow: close invalidates the inspector");
    pump(50);

    // Drain the pools the real viewer uses (foreground Decode + Background
    // neighbor preloads) before deleting the fixture directory.
    auto &sched = TaskScheduler::instance();
    CHECK(sched.drain(TaskScheduler::PoolType::DecodePool, std::chrono::seconds(15)),
          "pixel workflow: Decode pool drains before fixture removal");
    CHECK(sched.drain(TaskScheduler::PoolType::MetadataPool, std::chrono::seconds(15)),
          "pixel workflow: Background pool drains before fixture removal");
    dir.removeRecursively();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // 状态隔离：MainWindow 启动会执行 restoreLastSession()（QSettings +
    // AppState），如果读到真实用户/上一次测试的会话，会异步打开任意目录并
    // 触发解码，与本测试的确定性流程竞争（曾导致 ~50% flaky）。测试必须在
    // 干净的持久化状态下运行（先例：test_appstate.cpp）。
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("mviewer-workflow-ux-test");
    QCoreApplication::setApplicationName("mviewer-workflow-ux-test");
    QSettings().clear();
    {
        const QString cfg = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        if (!cfg.isEmpty())
            QDir(cfg).removeRecursively();
    }

    // 平台限制规避（非产品逻辑）：offscreen 平台下多个 worker 线程并发执行
    // 全分辨率 QImageReader::read() 会死锁 DecodePool（M3 起已知，见
    // ImageRepository.cpp loadDirectoryAsyncImpl 注释）。本测试验证的是
    // Workflow 状态机而不是解码并发，串行化解码池即可稳定复现用户流程。
    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(TaskScheduler::Priority::UI, 1);
    sched.setQueueMaxThreads(TaskScheduler::Priority::Decode, 1);
    sched.setQueueMaxThreads(TaskScheduler::Priority::Thumbnail, 1);
    sched.setQueueMaxThreads(TaskScheduler::Priority::Background, 1);

    // 测试用临时目录：5 张不同颜色 PNG，文件名保证排序稳定。
    QDir tmp(QDir::tempPath());
    const QString dirName = QStringLiteral("mviewer_wf_ux");
    tmp.mkpath(dirName);
    QDir workDir(tmp.filePath(dirName));
    const QList<QColor> colors = {QColor(200, 40, 40), QColor(40, 200, 40), QColor(40, 40, 200),
                                  QColor(200, 200, 40), QColor(40, 200, 200)};
    QStringList paths;
    for (int i = 0; i < colors.size(); ++i)
    {
        // paths[4] is intentionally non-square (19x13) so the Workflow 1
        // metadata-histogram regression can compare two gallery images of
        // different dimensions; paths[0] and paths[2] stay 32x32 for Workflow 2.
        const int w = i == 4 ? 19 : 32;
        const int h = i == 4 ? 13 : 32;
        paths << writePng(workDir, QStringLiteral("wf_%1.png").arg(i, 3, 10, QChar('0')),
                          colors[i], w, h);
    }

    workflow1_browse(workDir.absolutePath(), paths);
    workflow3_session_restore(workDir.absolutePath(), paths.first());
    workflow2_compare(paths[0], paths[2]);
    workflow10_compare_canvas(paths[0], paths[2]);
    workflow5_export_current_output_directory(workDir.absolutePath());
    workflow4_list_scaling(workDir.absolutePath());
    workflow6_metadata_dual_consumer(workDir.absolutePath());
    workflow7_stale_preload_cancellation(workDir.absolutePath());
    workflow8_preview_scaled_load(workDir.absolutePath());
    workflow9_pixel_inspector_lifecycle(workDir.absolutePath());

    for (const QString &p : paths)
        QFile::remove(p);
    tmp.rmdir(dirName);

    if (g_failures > 0)
    {
        std::cout << "workflow_ux_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "workflow_ux_tests: PASS\n";
    return 0;
}
