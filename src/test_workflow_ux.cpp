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
#include "imagelistmodel.h"
#include "mainwindow.h"
#include "metadataoverlay.h"
#include "previewpanel.h"
#include "runtime_storage.h"
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
#include <QFileDialog>
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
#include <QPointer>
#include <QProgressDialog>
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

QString appConfigFile(const QString &name)
{
    return mviewer::runtime::filePath(QStandardPaths::AppConfigLocation, name);
}

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

// Release-gated Background blocker used by the real MainWindow report export
// workflow.  The test keeps the only Background worker occupied so the report
// task is provably queued before the cancel button is pressed.
struct BackgroundBlocker
{
    struct Control
    {
        std::mutex mtx;
        std::condition_variable cv;
        bool released = false;
        std::atomic<bool> entered{false};
    };

    std::shared_ptr<Control> control = std::make_shared<Control>();
    TaskScheduler::TaskHandle task;

    BackgroundBlocker()
    {
        auto c = control;
        task = TaskScheduler::instance().submit(
            TaskScheduler::Priority::Background,
            [c](const TaskScheduler::TaskContext &)
            {
                c->entered.store(true, std::memory_order_release);
                std::unique_lock<std::mutex> lk(c->mtx);
                c->cv.wait(lk, [c] { return c->released; });
            });
    }

    void release()
    {
        std::lock_guard<std::mutex> lk(control->mtx);
        control->released = true;
        control->cv.notify_all();
    }

    ~BackgroundBlocker()
    {
        release();
    }
    BackgroundBlocker(const BackgroundBlocker &) = delete;
    BackgroundBlocker &operator=(const BackgroundBlocker &) = delete;
};

// The offscreen Qt platform cannot safely re-enter QProgressDialog's private
// button connection. The test still locates that real button, then cancels the
// exact queued report handle after exercising the public canceled state.
TaskScheduler::TaskHandle newestBackgroundTask(TaskScheduler::TaskId excludedId)
{
    auto &sched = TaskScheduler::instance();
    uint64_t submitted = 0;
    for (const TaskScheduler::PoolType pool : {TaskScheduler::MetadataPool,
                                               TaskScheduler::DecodePool,
                                               TaskScheduler::ThumbnailPool,
                                               TaskScheduler::AnalysisPool,
                                               TaskScheduler::IOPool})
        submitted += sched.metrics(pool).submitted;

    TaskScheduler::TaskHandle newest;
    for (TaskScheduler::TaskId id = 1; id <= submitted + 32; ++id)
    {
        const auto handle = sched.handle(id);
        if (!handle || handle->id == excludedId ||
            handle->priority != TaskScheduler::Priority::Background)
            continue;
        if (!newest || handle->id > newest->id)
            newest = handle;
    }
    return newest;
}

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
#include "test_workflow_ux_cases.inc"
int main(int argc, char **argv)
{
    // Force QFileDialog::getSaveFileName through Qt's event-loop-backed dialog
    // so the report workflow can deterministically observe nested-loop UI
    // heartbeats in the headless acceptance run.
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);

    // 状态隔离：MainWindow 启动会执行 restoreLastSession()（QSettings +
    // AppState），如果读到真实用户/上一次测试的会话，会异步打开任意目录并
    // 触发解码，与本测试的确定性流程竞争（曾导致 ~50% flaky）。测试必须在
    // 干净的持久化状态下运行（先例：test_appstate.cpp）。
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("mviewer-workflow-ux-test");
    QCoreApplication::setApplicationName("mviewer-workflow-ux-test");
    mviewer::runtime::configureSettings();
    QSettings().clear();
    {
        const QString cfg = mviewer::runtime::writableDirectory(QStandardPaths::AppConfigLocation);
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
    workflow11_compare_fullscreen(paths[0], paths[2]);
    workflow12_compare_mixed_fit(workDir.absolutePath());
    workflow2_compare(paths[0], paths[2]);
    workflow10_compare_canvas(paths[0], paths[2]);
    workflow5_export_current_output_directory(workDir.absolutePath());
    workflow4_list_scaling(workDir.absolutePath());
    workflow6_metadata_dual_consumer(workDir.absolutePath());
    workflow7_stale_preload_cancellation(workDir.absolutePath());
    workflow8_preview_scaled_load(workDir.absolutePath());
    workflow9_pixel_inspector_lifecycle(workDir.absolutePath());
    // Keep the report workflow terminal: Qt's offscreen QFileDialog leaves
    // internal modal widgets that deadlock MainWindow destruction. The test
    // closes its window and lets process teardown reclaim it after assertions.
    workflow13_compare_report_export_nonblocking(workDir.absolutePath());

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
