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
#include "core/image/Decoder.h"
#include "core/metadata/MetadataIndexer.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "directorytree.h"
#include "directorymodel.h"
#include "exportdialog.h"
#include "imageviewer.h"
#include "mainwindow.h"
#include "searchpanel.h"
#include "previewpanel.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"
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

#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
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

QString writePng(const QDir &dir, const QString &name, QColor color)
{
    const QString path = dir.filePath(name);
    QImage img(32, 32, QImage::Format_RGB32);
    img.fill(color);
    img.save(path, "PNG");
    return path;
}

// Minimal little-endian TIFF (DNG-like) carrying ISO/Make/Model/LensModel so
// parseRawMetadata extracts real camera/lens/ISO fields.
bool writeFakeDng(const std::string &path, const std::string &make,
                  const std::string &model, const std::string &lens, uint16_t iso)
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
    writeEntry(0x8827, 3, 1, iso);                                       // ISO (SHORT inline)
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
        CHECK(!thumbnailSizeSlider->isEnabled(),
              "Large preset disables the thumbnail size slider");
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
        CHECK(thumbnailSizeSlider->isEnabled(),
              "Thumbnail re-enables the thumbnail size slider");
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
        CHECK(thumbnailPanel && thumbnailPanel->viewMode() == ThumbnailPanel::LargeIcon &&
                  thumbnailPanel->thumbSize() == 240 && thumbnailSizeSlider &&
                  thumbnailSizeSlider->value() == 240 && !thumbnailSizeSlider->isEnabled() &&
                  viewModeCombo &&
                  viewModeCombo->currentData().toInt() == ThumbnailPanel::LargeIcon,
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
    pump(100);

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

        const QString initialMetrics = metricsLabel->text();
        const QString initialInspector =
            inspector->item(1, 2) ? inspector->item(1, 2)->text() : QString();

        thresholdSlider->setValue(200);
        pump(20);
        const QString thresholdMetrics = metricsLabel->text();
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
        pump(20);
        CHECK(metricsLabel->text() == initialMetrics,
              "threshold release refreshes deferred metrics");
        thresholdSlider->setValue(200);
        pump(20);

        brightnessSlider->setValue(40);
        pump(20);
        const QString adjustedMetrics = metricsLabel->text();
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

        brightnessSlider->setSliderDown(true);
        brightnessSlider->setValue(80);
        pump(20);
        const QString draggingMetrics = metricsLabel->text();
        const QString draggingInspector =
            inspector->item(1, 2) ? inspector->item(1, 2)->text() : QString();
        CHECK(draggingMetrics == adjustedMetrics && draggingInspector != adjustedInspector,
              "adjustment drag updates Inspector while deferring metrics");
        brightnessSlider->setSliderDown(false);
        pump(20);
        CHECK(metricsLabel->text() != draggingMetrics,
              "adjustment release refreshes deferred metrics");

        resetButton->click();
        pump(20);
        const QString resetInspector =
            inspector->item(1, 2) ? inspector->item(1, 2)->text() : QString();
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
        pipeline.setDecodeFn([totalDecodeStarts, decodePaths, decodePathsMutex](
                                 const std::string &path, int)
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
                  << totalDecodeStarts->load(std::memory_order_relaxed)
                  << " total, " << uniqueDecodePaths << " unique paths\n";
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
    const QString sourcePath = writePng(sourceDir, QStringLiteral("export_source.png"),
                                        QColor(80, 120, 180));
    const QString sourceReport = sourceDir.filePath(QStringLiteral("export_report.csv"));
    const QString outputReport = outputDir.filePath(QStringLiteral("export_report.csv"));
    QFile::remove(sourceReport);
    QFile::remove(outputReport);

    {
        ExportDialog dialog(QStringList{sourcePath});
        auto *dirEdit =
            dialog.findChild<QLineEdit *>(QStringLiteral("exportOutputDirectoryEdit"));
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
    while (mviewer::core::MetadataIndexer::instance().size() == 0 &&
           startTimer.elapsed() < 4000)
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
        paths << writePng(workDir, QStringLiteral("wf_%1.png").arg(i, 3, 10, QChar('0')),
                          colors[i]);

    workflow1_browse(workDir.absolutePath(), paths);
    workflow3_session_restore(workDir.absolutePath(), paths.first());
    workflow2_compare(paths[0], paths[2]);
    workflow5_export_current_output_directory(workDir.absolutePath());
    workflow4_list_scaling(workDir.absolutePath());
    workflow6_metadata_dual_consumer(workDir.absolutePath());

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
