// Review markup: drag a rectangle, Shift+drag an arrow, and export a review
// image with the markup burned in plus the JSON sidecar next to the source.
#include "annotationstore.h"
#include "imageviewer.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QTemporaryDir>

#include <functional>
#include <iostream>

namespace
{
int g_failures = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
            std::cout << "[ok] " << msg << "\n";                                                   \
        else                                                                                       \
        {                                                                                          \
            std::cout << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n";         \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

void pump(int ms = 20)
{
    QElapsedTimer timer;
    timer.start();
    do
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    while (timer.elapsed() < ms);
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 8000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
        pump(10);
    return predicate();
}

void sendDrag(ImageViewer &viewer, const QPointF &a, const QPointF &b, Qt::KeyboardModifiers mods)
{
    QMouseEvent press(QEvent::MouseButtonPress, a, viewer.mapToGlobal(a), Qt::LeftButton,
                      Qt::LeftButton, mods);
    QApplication::sendEvent(&viewer, &press);
    QMouseEvent move(QEvent::MouseMove, b, viewer.mapToGlobal(b), Qt::NoButton, Qt::LeftButton,
                     mods);
    QApplication::sendEvent(&viewer, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, b, viewer.mapToGlobal(b), Qt::LeftButton,
                        Qt::NoButton, mods);
    QApplication::sendEvent(&viewer, &release);
}

int annotationCount(const QString &sidecarPath)
{
    QFile file(sidecarPath);
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject())
        return -1;
    return doc.object().value(QStringLiteral("annotations")).toArray().size();
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    setbuf(stdout, nullptr);
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir created");
    if (!tmp.isValid())
        return 1;

    QImage source(200, 120, QImage::Format_RGB32);
    source.fill(Qt::gray);
    const QString path = tmp.filePath("markup.png");
    CHECK(source.save(path, "PNG"), "fixture written");
    const QString sidecar = QFileInfo(path).absolutePath() + QLatin1Char('/') +
                            QFileInfo(path).completeBaseName() +
                            QStringLiteral(".mviewer-annotations.json");
    const QString review = QFileInfo(path).absolutePath() + QLatin1Char('/') +
                           QFileInfo(path).completeBaseName() + QStringLiteral("-review.png");
    QFile::remove(sidecar);
    QFile::remove(review);

    ImageViewer viewer;
    viewer.resize(640, 400);
    viewer.show();
    pump(30);
    viewer.setImage(path);
    CHECK(waitFor([&] { return viewer.displaySize().isValid(); }), "viewer decoded the fixture");

    viewer.setAnnotateMode(true);
    CHECK(viewer.isAnnotateMode(), "A arms the markup tool");

    sendDrag(viewer, QPointF(120, 100), QPointF(320, 240), Qt::NoModifier);
    pump(20);
    CHECK(annotationCount(sidecar) == 1, "a committed drag writes one annotation");

    sendDrag(viewer, QPointF(200, 200), QPointF(420, 300), Qt::ShiftModifier);
    pump(20);
    CHECK(annotationCount(sidecar) == 2, "Shift+drag adds a second (arrow) annotation");

    // The sidecar is the shareable artifact; the store reloads it.
    mviewer::ui::AnnotationStore store(path);
    CHECK(store.load() && store.annotations().size() == 2, "the sidecar round-trips");

    viewer.exportAnnotatedImage();
    pump(50);
    QImage exported;
    CHECK(exported.load(review), "the review image is written next to the source");
    CHECK(exported.size() == source.size(), "the review image keeps the source geometry");

    // Right-click on the first box removes it (topmost hit wins).
    viewer.setAnnotateMode(false);
    viewer.setAnnotateMode(true);
    pump(10);
    CHECK(annotationCount(sidecar) == 2, "re-arming keeps the saved markup");
    const double sx = 200.0, sy = 160.0; // inside both primitives, in widget space
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(sx, sy),
                      viewer.mapToGlobal(QPointF(sx, sy)), Qt::RightButton, Qt::RightButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&viewer, &press);
    pump(20);
    CHECK(annotationCount(sidecar) == 1, "right-click deletes the annotation under the cursor");

    if (g_failures != 0)
    {
        std::cout << "viewer_annotate_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "viewer_annotate_tests: PASS\n";
    return 0;
}
