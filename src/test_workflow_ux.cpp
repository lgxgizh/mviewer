// Beta regression — end-to-end user workflow tests (from the 2026-07 external review, maintained
// since M58).
//
// Review requirement: do not only verify "can Compare open", verify "is Compare pleasant to use";
// walk every complete user workflow and check for waits, out-of-sync state and zoom errors.
//
// Workflow 1 (main browse flow) — real MainWindow:
//   open folder → browse images → keyboard navigation (←/→/Home/End/PageUp/PageDown)
//   → zoom (100%/zoom in) → restore (Fit) → close.
//   Filter rebuilds also verify the current selection projects onto the visible sequence and
//   that no match clears the SSOT and folder title (driven by the real ThumbnailPanel selection).
//   The initial layout also verifies the gallery toolbar takes space, is visible and usable, incl.
//   the minimum window. Assertions: SelectionModel (SSOT) and navigation state stay in sync, zoom
//   values are right, bounds hold.
//
// Workflow 2 (main compare flow) — real CompareWorkspace + real key events:
//   two-image Compare → mode switches: Flicker (B) / Split (S) / Overlay (O) / Checkerboard (K) /
//   Highlight Diff (H) → exclusive modes leave nothing behind (review "out-of-sync state" check) →
//   Space momentary flicker → Esc exits Compare (real QDialog path) → keep browsing.
//
// Maps one-to-one to the "Browsing / Compare / View" items of docs/beta_checklist.md.

#include "Language.h"
#include "analysispanel.h"
#include "appstate.h"
#include "batchdialog.h"
#include "compareworkspace.h"
#include "core/analysis/ImageOverlay.h"
#include "core/analysis/PixelGrid.h"
#include "core/cache/CacheManager.h"
#include "core/compare/DifferenceEngine.h"
#include "core/image/Decoder.h"
#include "core/image/ImageAdjust.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h" // Compare workflow exercises metadata-only placeholders.
#include "core/image/ImageRepository.h"
#include "core/image/QtConvert.h"
#include "core/metadata/MetadataIndexer.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "directorymodel.h"
#include "directorytree.h"
#include "exportdialog.h"
#include "exportmessages.h"
#include "imagelistmodel.h"
#include "imageviewer.h"
#include "mainwindow.h"
#include "metadataoverlay.h"
#include "metadatapanel.h"
#include "preferencesdialog.h"
#include "previewpanel.h"
#include "runtime_storage.h"
#include "searchpanel.h"
#include "selectionmodel.h"
#include "shortcutdigits.h"
#include "thumbnailpanel.h"
#include "widgets/histogramwidget.h"
#include "widgets/infooverlay.h"
#include "widgets/rawimageview.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHash>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMargins>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QRect>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextCursor>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
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
#include <string>
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

// Windows delivers Shift+digit as the shifted symbol (Shift+2 -> Key_At)
// with the digit only in the native virtual key (VK '2' = 0x32).
void sendWindowsShiftedDigit(QWidget *w, Qt::Key symbol, int digit,
                             Qt::KeyboardModifiers mods = Qt::ShiftModifier)
{
    const auto vk = static_cast<quint32>(0x30 + digit);
    QKeyEvent press(QEvent::KeyPress, symbol, mods, 0, vk, 0);
    QApplication::sendEvent(w, &press);
    QKeyEvent release(QEvent::KeyRelease, symbol, mods, 0, vk, 0);
    QApplication::sendEvent(w, &release);
    pump(20);
}

static_assert(mviewer::ui::shortcutDigit(Qt::Key_Exclam, Qt::ShiftModifier, 0) == 1);
static_assert(mviewer::ui::shortcutDigit(Qt::Key_ParenRight, Qt::ShiftModifier, 0) == 0);
static_assert(mviewer::ui::shortcutDigit(Qt::Key_AsciiCircum, Qt::ShiftModifier, 0) == 6);
static_assert(mviewer::ui::shortcutDigit(Qt::Key_At, Qt::ShiftModifier, 0x32) == 2);
static_assert(mviewer::ui::shortcutDigit(Qt::Key_Exclam, Qt::NoModifier, 0) == -1);
static_assert(mviewer::ui::shortcutDigit(Qt::Key_5, Qt::NoModifier, 0) == 5);

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

// Sends KeyPress only (Space hold scenario).
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
        task = TaskScheduler::instance().submit(TaskScheduler::Priority::Analysis,
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

// Compare loading-feedback gate: occupy the sole Decode worker so the UI can
// be inspected while a real setImages() batch is pending. The blocker is
// release-gated rather than time-based, keeping the contract test independent
// of decoder/cache speed.
struct DecodeBlocker
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

    DecodeBlocker()
    {
        auto c = control;
        task =
            TaskScheduler::instance().submit(TaskScheduler::Priority::Decode,
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

    ~DecodeBlocker()
    {
        release();
    }
    DecodeBlocker(const DecodeBlocker &) = delete;
    DecodeBlocker &operator=(const DecodeBlocker &) = delete;
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
        task =
            TaskScheduler::instance().submit(TaskScheduler::Priority::Background,
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
    for (const TaskScheduler::PoolType pool :
         {TaskScheduler::MetadataPool, TaskScheduler::DecodePool, TaskScheduler::ThumbnailPool,
          TaskScheduler::AnalysisPool, TaskScheduler::IOPool})
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

QByteArray makeWorkflowAnimatedGif()
{
    // Deterministic red/green/blue frames. Keep this tiny fixture local to the
    // real Viewer workflow so the regression exercises the deployed qgif path.
    QByteArray gif("GIF89a", 6);
    gif.append("\x04\x00\x04\x00\x80\x00\x00", 7);
    const char globalPalette[] = {char(0xff), 0, 0, 0, 0, 0};
    gif.append(globalPalette, sizeof(globalPalette));
    gif.append("\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00", 19);

    const char colors[][3] = {{char(0xff), 0, 0}, {0, char(0xff), 0}, {0, 0, char(0xff)}};
    for (int frame = 0; frame < 3; ++frame)
    {
        gif.append("\x21\xf9\x04\x00", 4);
        gif.append(static_cast<char>((frame + 1) * 10));
        gif.append("\x00\x00\x00", 3);
        gif.append('\x2c');
        gif.append("\x00\x00\x00\x00\x04\x00\x04\x00", 8);
        gif.append(frame == 0 ? '\x00' : '\x80');
        if (frame != 0)
        {
            gif.append(colors[frame], 3);
            gif.append("\x00\x00\x00", 3);
        }
        gif.append("\x08\x09\x00\x01\x08\x1c\x48\xb0\x20\x80\x80\x00", 12);
    }
    gif.append('\x3b');
    return gif;
}

QString writeAnimatedGif(const QDir &dir, const QString &name)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    const QByteArray fixture = makeWorkflowAnimatedGif();
    if (!file.open(QIODevice::WriteOnly) || file.write(fixture) != fixture.size())
        return {};
    return path;
}

void putWorkflowTiff16(QByteArray &data, int offset, uint16_t value)
{
    data[offset] = static_cast<char>(value & 0xff);
    data[offset + 1] = static_cast<char>((value >> 8) & 0xff);
}

void putWorkflowTiff32(QByteArray &data, int offset, uint32_t value)
{
    for (int byte = 0; byte < 4; ++byte)
        data[offset + byte] = static_cast<char>((value >> (byte * 8)) & 0xff);
}

QByteArray makeWorkflowMultipageTiff()
{
    constexpr int pageCount = 3;
    constexpr int ifdStart = 8;
    constexpr int ifdSize = 2 + 9 * 12 + 4;
    constexpr int bitsStart = ifdStart + pageCount * ifdSize;
    constexpr int pixelsStart = bitsStart + pageCount * 6;
    QByteArray tiff(pixelsStart + pageCount * 48, '\0');
    tiff[0] = 'I';
    tiff[1] = 'I';
    putWorkflowTiff16(tiff, 2, 42);
    putWorkflowTiff32(tiff, 4, ifdStart);
    for (int page = 0; page < pageCount; ++page)
    {
        const int ifd = ifdStart + page * ifdSize;
        putWorkflowTiff16(tiff, ifd, 9);
        const int entries = ifd + 2;
        auto entry = [&](int n, uint16_t tag, uint16_t type, uint32_t count, uint32_t value)
        {
            const int at = entries + n * 12;
            putWorkflowTiff16(tiff, at, tag);
            putWorkflowTiff16(tiff, at + 2, type);
            putWorkflowTiff32(tiff, at + 4, count);
            putWorkflowTiff32(tiff, at + 8, value);
        };
        entry(0, 256, 3, 1, 4);
        entry(1, 257, 3, 1, 4);
        entry(2, 258, 3, 3, bitsStart + page * 6);
        entry(3, 259, 3, 1, 1);
        entry(4, 262, 3, 1, 2);
        entry(5, 273, 4, 1, pixelsStart + page * 48);
        entry(6, 277, 3, 1, 3);
        entry(7, 278, 4, 1, 4);
        entry(8, 279, 4, 1, 48);
        putWorkflowTiff32(tiff, entries + 9 * 12, page + 1 < pageCount ? ifd + ifdSize : 0);
        for (int channel = 0; channel < 3; ++channel)
            putWorkflowTiff16(tiff, bitsStart + page * 6 + channel * 2, 8);
        for (int pixel = 0; pixel < 16; ++pixel)
        {
            const int at = pixelsStart + page * 48 + pixel * 3;
            tiff[at] = page == 0 ? char(0xff) : 0;
            tiff[at + 1] = page == 1 ? char(0xff) : 0;
            tiff[at + 2] = page == 2 ? char(0xff) : 0;
        }
    }
    return tiff;
}

QString writeMultipageTiff(const QDir &dir, const QString &name)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    const QByteArray fixture = makeWorkflowMultipageTiff();
    if (!file.open(QIODevice::WriteOnly) || file.write(fixture) != fixture.size())
        return {};
    return path;
}

struct ContextMenuSnapshot
{
    QStringList labels;
    QStringList enabledLabels;
};

ContextMenuSnapshot contextMenuSnapshot(ImageViewer &viewer)
{
    ContextMenuSnapshot snapshot;
    QTimer::singleShot(0,
                       [&snapshot]()
                       {
                           auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                           if (!menu)
                               return;
                           for (QAction *action : menu->actions())
                               if (!action->isSeparator())
                               {
                                   snapshot.labels.append(action->text());
                                   if (action->isEnabled())
                                       snapshot.enabledLabels.append(action->text());
                               }
                           menu->close();
                       });
    const QPoint local(viewer.width() / 2, viewer.height() / 2);
    QContextMenuEvent event(QContextMenuEvent::Mouse, local, viewer.mapToGlobal(local));
    QApplication::sendEvent(&viewer, &event);
    return snapshot;
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

// Find a CompareWorkspace mode checkbox by text prefix (members are private; the text is a public
// UI contract).
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

// ─── Workflow 1: open folder → browse → keyboard nav → zoom in → restore → close ─────────
// M54 keeps selected preview work on the foreground Decode path; the included
// workflow cases assert that contract along with the existing UX flows.
// Workflow1/16 also verify empty-state command availability, Viewer zoom state,
// slideshow/navigation state, and Browse boundaries.
#include "test_workflow_ux_cases.inc"
int main(int argc, char **argv)
{
    // Force QFileDialog::getSaveFileName through Qt's event-loop-backed dialog
    // so the report workflow can deterministically observe nested-loop UI
    // heartbeats in the headless acceptance run.
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);

    // State isolation: MainWindow startup runs restoreLastSession() (QSettings +
    // AppState); reading a real user or previous-test session would asynchronously open some folder
    // and trigger decodes that race this deterministic flow (once ~50% flaky). The test must run
    // with clean persisted state (precedent: test_appstate.cpp).
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("mviewer-workflow-ux-test");
    QCoreApplication::setApplicationName("mviewer-workflow-ux-test");
    mviewer::runtime::configureSettings();
    QSettings().clear();
    mviewer::ui::Language::apply(mviewer::ui::LanguageMode::English);
    {
        const QString cfg = mviewer::runtime::writableDirectory(QStandardPaths::AppConfigLocation);
        if (!cfg.isEmpty())
            QDir(cfg).removeRecursively();
    }

    // Platform workaround (not product logic): on the offscreen platform several worker threads
    // running full-resolution QImageReader::read() concurrently deadlock the DecodePool (known
    // since M3, see the ImageRepository.cpp loadDirectoryAsyncImpl comment). This test verifies the
    // workflow state machine, not decode concurrency, so a serialized decode pool reproduces the
    // flow reliably.
    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(TaskScheduler::Priority::UI, 1);
    sched.setQueueMaxThreads(TaskScheduler::Priority::Decode, 1);
    sched.setQueueMaxThreads(TaskScheduler::Priority::Thumbnail, 1);
    sched.setQueueMaxThreads(TaskScheduler::Priority::Background, 1);

    // Test temp folder: 5 PNGs of different colors; file names keep the sort order stable.
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
        paths << writePng(workDir, QStringLiteral("wf_%1.png").arg(i, 3, 10, QChar('0')), colors[i],
                          w, h);
    }

    workflow18_shell_copy_and_export_text();
    workflow1_browse(workDir.absolutePath(), paths);
    workflow3_session_restore(workDir.absolutePath(), paths.first());
    workflow11_compare_fullscreen(paths[0], paths[2]);
    workflow15_compare_nonfirst_pair_navigation(workDir.absolutePath());
    workflow12_compare_mixed_fit(workDir.absolutePath());
    workflow2_compare(paths[0], paths[2]);
    workflow19_layout_chrome(paths[0], paths[1], paths[2]);
    workflow10_compare_canvas(paths[0], paths[2]);
    workflow5_export_current_output_directory(workDir.absolutePath());
    workflow17_batch_dialog(workDir.absolutePath());
    workflow4_list_scaling(workDir.absolutePath());
    workflow6_metadata_dual_consumer(workDir.absolutePath());
    workflow7_stale_preload_cancellation(workDir.absolutePath());
    workflow8_preview_scaled_load(workDir.absolutePath());
    workflow9_pixel_inspector_lifecycle(workDir.absolutePath());
    workflow16_multiframe_view_continuity(workDir.absolutePath());
    workflow14_compare_loading_feedback(workDir.absolutePath());
    // Remaining ADR-017 process-exit exception: Qt's offscreen QFileDialog
    // leaves internal modal widgets that deadlock MainWindow destruction. The
    // report workflow keeps that window on the heap until process teardown.
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
