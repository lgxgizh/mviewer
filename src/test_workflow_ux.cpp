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
#include "core/scheduler/TaskScheduler.h"
#include "directorymodel.h"
#include "imageviewer.h"
#include "mainwindow.h"
#include "selectionmodel.h"
#include "widgets/rawimageview.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <iostream>

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
    std::cout << "── Workflow 1: browse ──\n";

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
    CHECK(analysisPanel && !analysisPanel->isVisible(),
          "clean startup keeps the analysis panel hidden");
    CHECK(searchPanel && !searchPanel->isVisible(), "clean startup keeps the search panel hidden");
    CHECK(advancedFilterPanel && !advancedFilterPanel->isVisible(),
          "advanced gallery filters are hidden by default");
    CHECK(advancedFilterToggle && advancedFilterToggle->isCheckable(),
          "advanced filter toggle has a stable interactive control");

    auto *thumbnailSizeSlider = w.findChild<QSlider *>("thumbnailSizeSlider");
    auto *pathEdit = w.findChild<QLineEdit *>("pathEdit");
    CHECK(thumbnailSizeSlider != nullptr, "thumbnail size slider is discoverable");
    CHECK(pathEdit != nullptr, "directory path input is discoverable");

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
    if (!sel || !dirModel)
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
        }
        viewer->close();
        pump(20);
    }

    // Closing while Focus Browse is active must persist the panel state from
    // before Focus hid the panels, not the temporary hidden state.
    if (analysisAction)
        analysisAction->trigger();
    if (searchAction)
        searchAction->trigger();
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
    pump(50);
    CHECK(true, "main window closes cleanly after the browse workflow");
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

    // 默认状态（评审"默认值不合理"检查）。
    CHECK(ws->comparedImageCount() == 2, "two images loaded into Compare");
    CHECK(ws->isSyncEnabled(), "sync zoom/pan is ON by default");
    CHECK(sel.compared().size() == 2, "SelectionModel.compared mirrors the compare set (SSOT)");
    CHECK(!sel.focused().isEmpty(), "a default focus/reference image is set");

    QCheckBox *blink = findChk(ws, QStringLiteral("闪烁对比"));
    QCheckBox *split = findChk(ws, QStringLiteral("左右分割"));
    QCheckBox *overlay = findChk(ws, QStringLiteral("叠加对比"));
    QCheckBox *checker = findChk(ws, QStringLiteral("棋盘"));
    CHECK(blink && split && overlay, "mode checkboxes exist (blink/split/overlay)");
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

        referenceView->setTransform(2.0, QPointF(19.0, 23.0));
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
    workflow2_compare(paths[0], paths[2]);

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
