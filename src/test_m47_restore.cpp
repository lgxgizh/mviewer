// M47 Phase 5 — transactional async Workspace/Project restore tests.
//
// The workspace/project open path no longer reads + deserializes on the UI
// thread: openWorkspaceFile/openProjectFile run the file I/O and JSON parse on
// a background worker, and the UI applies the parsed document atomically for
// the CURRENT generation only.
//   R1  async success + ordering: the call returns BEFORE the restore is
//       applied (status shows "Opening…"), the apply lands on a later UI
//       delivery, and the directory + compare context come back.
//   R1b project variant: openProjectFile restores the embedded workspace and
//       reports the project terminal message.
//   R2  failure preserves the session: a missing file and a corrupt file each
//       surface the error dialog WITHOUT touching the current directory or the
//       compared-image set (transactional restore).
//   R3  latest-intent supersession: A then B back-to-back — only B's restore
//       lands; A's stale delivery is dropped by the generation guard.
//   R4  destroy mid-restore: MainWindow destroyed while the worker runs — no
//       crash, deliveries dropped, scheduler drains.

#include "core/project/ProjectSerializer.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/workspace/WorkspaceSerializer.h"
#include "directorymodel.h"
#include "mainwindow.h"
#include "workspacemodel.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>

#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>

static int g_failures = 0;

#define CHECK(c, m)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            std::printf("FAIL: %s\n", m);                                                          \
            std::fflush(stdout);                                                                   \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

#define MARK(t)                                                                                    \
    do                                                                                             \
    {                                                                                              \
        std::printf("%s\n", t);                                                                    \
        std::fflush(stdout);                                                                       \
    } while (false)

namespace
{

void pump(int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
        QApplication::processEvents(QEventLoop::AllEvents, 1);
}

bool waitTrue(const std::function<bool()> &pred, int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 1);
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return pred();
}

// The terminal line ("Workspace opened" / "Project opened" / "Failed to open…")
// is not sticky. changeDirectory's scan later emits sequenceChanged, and
// mainwindow_ui_connections_browse.cpp posts "Loaded N images" (2500 ms). That
// write often runs in the same processEvents() as the restore apply, so a poll
// of currentMessage() never observes the terminal text even though it was
// shown. Latch messageChanged instead of sampling the bar.
class StatusLatch
{
  public:
    StatusLatch() = default;
    StatusLatch(const StatusLatch &) = delete;
    StatusLatch &operator=(const StatusLatch &) = delete;

    void arm(QStatusBar *bar)
    {
        disarm();
        if (!bar)
            return;
        m_messages.push_back(bar->currentMessage());
        m_conn = QObject::connect(bar, &QStatusBar::messageChanged, bar,
                                  [this](const QString &text) { m_messages.push_back(text); });
    }

    void disarm()
    {
        if (m_conn)
            QObject::disconnect(m_conn);
        m_conn = {};
        m_messages.clear();
    }

    ~StatusLatch()
    {
        disarm();
    }

    bool seen(const QString &fragment) const
    {
        for (const QString &text : m_messages)
        {
            if (text.contains(fragment))
                return true;
        }
        return false;
    }

  private:
    QMetaObject::Connection m_conn;
    QStringList m_messages;
};

bool waitForStatus(StatusLatch &latch, QStatusBar *bar, const QString &fragment, int ms)
{
    latch.arm(bar);
    return waitTrue([&] { return latch.seen(fragment); }, ms);
}

void writePng(const QString &path)
{
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(QColor(60, 90, 120));
    img.save(path, "PNG");
}

// A small but structurally complete workspace document: one folder with one
// image plus the explicit compared-image list.
mviewer::domain::Workspace makeWorkspace(const QString &root, const QStringList &compared)
{
    mviewer::domain::Workspace ws;
    ws.rootPath = root.toStdString();
    mviewer::domain::Folder folder;
    folder.path = root.toStdString();
    folder.name = QFileInfo(root).fileName().toStdString();
    mviewer::domain::ImageMetadata meta;
    meta.filePath = (root + "/a.png").toStdString();
    meta.fileName = "a.png";
    meta.width = 64;
    meta.height = 64;
    meta.format = "PNG";
    meta.channels = 3;
    folder.imageSet.images.push_back(meta);
    ws.folders.push_back(folder);
    for (const QString &p : compared)
        ws.comparedImages.push_back(p.toStdString());
    return ws;
}

QString writeWorkspaceFile(const QString &dir, const QString &name,
                           const mviewer::domain::Workspace &ws)
{
    const QString path = dir + "/" + name;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
        f.write(QByteArray::fromStdString(mviewer::core::serializeWorkspace(ws)));
    f.close();
    return path;
}

QString writeProjectFile(const QString &dir, const QString &name,
                         const mviewer::domain::Workspace &ws)
{
    const QString path = dir + "/" + name;
    mviewer::domain::Project project;
    project.name = "proj";
    project.workspace = ws;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
        f.write(QByteArray::fromStdString(mviewer::core::serializeProject(project)));
    f.close();
    return path;
}

// Repeatedly closes any modal dialog while a restore error is delivered, so
// the async failure path's QMessageBox cannot block the test.
void installModalCloser(QTimer &closer)
{
    closer.setInterval(100);
    QObject::connect(&closer, &QTimer::timeout,
                     []()
                     {
                         if (QWidget *modal = QApplication::activeModalWidget())
                             modal->close();
                     });
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QTemporaryDir tmp;
    if (!tmp.isValid())
        return 1;
    const QString dir = tmp.path();
    const QString rootA = dir + "/rootA";
    const QString rootB = dir + "/rootB";
    QDir().mkpath(rootA);
    QDir().mkpath(rootB);
    writePng(rootA + "/a.png");
    writePng(rootB + "/a.png");
    writePng(rootB + "/b.png");

    const QString wsA = writeWorkspaceFile(dir, "a.mvws", makeWorkspace(rootA, {rootA + "/a.png"}));
    const QString wsB = writeWorkspaceFile(dir, "b.mvws", makeWorkspace(rootB, {rootB + "/b.png"}));
    const QString projA =
        writeProjectFile(dir, "a.mvproj", makeWorkspace(rootA, {rootA + "/a.png"}));

    // ── R1: async success + ordering ─────────────────────────────────────────
    {
        MARK("R1 start");
        MainWindow w;
        w.show();
        w.openWorkspaceFile(wsA);
        // The call returned without blocking: the read+deserialize happened on
        // a background worker, so the apply cannot have landed yet.
        const QString started = w.statusBar()->currentMessage();
        printf("  R1: status right after call: '%s'\n", started.toUtf8().constData());
        std::fflush(stdout);
        CHECK(started.contains(QStringLiteral("Opening workspace")),
              "R1: the async open announces itself immediately");
        CHECK(!started.contains(QStringLiteral("Workspace opened")),
              "R1: the restore is NOT applied synchronously (async worker path)");
        StatusLatch opened;
        CHECK(waitForStatus(opened, w.statusBar(), QStringLiteral("Workspace opened"), 30000),
              "R1: the parsed workspace lands on a later UI delivery");
        DirectoryModel *model = w.findChild<DirectoryModel *>();
        CHECK(model && model->currentDirectory() == rootA,
              "R1: the browsing directory is restored");
        WorkspaceModel *wm = w.findChild<WorkspaceModel *>();
        CHECK(wm && wm->comparedImages() == QStringList({rootA + "/a.png"}),
              "R1: the compare image set is restored");
    }

    // ── R1b: project variant ─────────────────────────────────────────────────
    {
        MARK("R1b start");
        MainWindow w;
        w.show();
        w.openProjectFile(projA);
        StatusLatch opened;
        CHECK(waitForStatus(opened, w.statusBar(), QStringLiteral("Project opened"), 30000),
              "R1b: the project's embedded workspace restores");
        DirectoryModel *model = w.findChild<DirectoryModel *>();
        CHECK(model && model->currentDirectory() == rootA,
              "R1b: the project restores the browsing directory");
    }

    // ── R2: failure preserves the live session ───────────────────────────────
    {
        MARK("R2 start");
        MainWindow w;
        w.show();
        w.openWorkspaceFile(wsA);
        StatusLatch opened;
        CHECK(waitForStatus(opened, w.statusBar(), QStringLiteral("Workspace opened"), 30000),
              "R2: baseline workspace applied");
        DirectoryModel *model = w.findChild<DirectoryModel *>();
        WorkspaceModel *wm = w.findChild<WorkspaceModel *>();
        const QString dirBefore = model ? model->currentDirectory() : QString();
        const QStringList cmpBefore = wm ? wm->comparedImages() : QStringList();

        // Missing file: the error surfaces, the session stays.
        {
            QTimer closer;
            installModalCloser(closer);
            closer.start();
            w.openWorkspaceFile(dir + "/no_such_file.mvws");
            // Wait (bounded) for the failure terminal instead of a fixed 4 s
            // pump: the assertion below reads that delivery, not whatever
            // status text is current after later UI updates.
            StatusLatch failed;
            (void)waitForStatus(failed, w.statusBar(), QStringLiteral("Failed to open workspace"),
                                15000);
            closer.stop();
            CHECK(failed.seen(QStringLiteral("Failed to open workspace")),
                  "R2: the failure terminal is reported (session untouched)");
        }
        CHECK(model && model->currentDirectory() == dirBefore,
              "R2: a missing file does not change the current directory");
        CHECK(wm && wm->comparedImages() == cmpBefore,
              "R2: a missing file does not change the compare image set");

        // Corrupt file: same transactional contract.
        {
            const QString bad = dir + "/bad.mvws";
            QFile f(bad);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                f.write("this is not a workspace document");
            f.close();
            QTimer closer;
            installModalCloser(closer);
            closer.start();
            w.openWorkspaceFile(bad);
            // Same bounded terminal wait: a corrupt document reports through the
            // same failure path (mainwindow_session.cpp: invalid or empty workspace file).
            StatusLatch failed;
            (void)waitForStatus(failed, w.statusBar(), QStringLiteral("Failed to open workspace"),
                                15000);
            closer.stop();
            CHECK(failed.seen(QStringLiteral("Failed to open workspace")),
                  "R2: a corrupt file reports the failure terminal");
        }
        CHECK(model && model->currentDirectory() == dirBefore,
              "R2: a corrupt file does not change the current directory");
        CHECK(wm && wm->comparedImages() == cmpBefore,
              "R2: a corrupt file does not change the compare image set");
    }

    // ── R3: latest-intent supersession (A then B) ────────────────────────────
    {
        MARK("R3 start");
        MainWindow w;
        w.show();
        // A's worker may finish before B is submitted; its queued delivery is
        // still dropped because B bumps the generation on the UI thread before
        // the event loop dispatches A's delivery.
        w.openWorkspaceFile(wsA);
        w.openWorkspaceFile(wsB);
        StatusLatch opened;
        CHECK(waitForStatus(opened, w.statusBar(), QStringLiteral("Workspace opened"), 30000),
              "R3: the final open lands");
        pump(500); // allow any stale A delivery to attempt landing
        DirectoryModel *model = w.findChild<DirectoryModel *>();
        WorkspaceModel *wm = w.findChild<WorkspaceModel *>();
        CHECK(model && model->currentDirectory() == rootB,
              "R3: ONLY the final generation's restore applies (A dropped)");
        CHECK(wm && wm->comparedImages() == QStringList({rootB + "/b.png"}),
              "R3: the compare set is the final request's");
    }

    // ── R4: destroy mid-restore ──────────────────────────────────────────────
    {
        MARK("R4 start");
        {
            MainWindow w;
            w.show();
            w.openWorkspaceFile(wsA);
            // Destroyed immediately: the worker / queued delivery may still be
            // in flight.
        }
        pump(1500);
        CHECK(waitTrue(
                  [&]
                  {
                      uint64_t pending = 0;
                      uint64_t active = 0;
                      for (int p = 0; p < 5; ++p)
                      {
                          const auto m = TaskScheduler::instance().metrics(
                              static_cast<TaskScheduler::PoolType>(p));
                          pending += m.pending;
                          active += m.active_tasks;
                      }
                      return pending + active == 0;
                  },
                  15000),
              "R4: pools drain after destroy mid-restore (no crash, no leak)");
    }

    std::printf("=== M47 transactional async restore tests: %s ===\n",
                g_failures == 0 ? "PASS" : "FAIL");
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
