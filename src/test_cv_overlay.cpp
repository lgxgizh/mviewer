// CV overlay extraction (item 2 God Class split): the box list and its JSON
// sidecar live in their own class now, so the contract is testable directly.
#include "cvoverlay.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

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
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    setbuf(stdout, nullptr);
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir created");
    if (!tmp.isValid())
        return 1;

    const QString path = tmp.filePath("boxes.json");
    QJsonObject root;
    QJsonArray boxes;
    boxes.append(QJsonArray{10.0, 20.0, 30.0, 40.0, "skin"});
    boxes.append(QJsonArray{5.0, 6.0, 0.0, 10.0});      // degenerate: rejected
    boxes.append(QJsonArray{1.0, 2.0});                 // too short: rejected
    boxes.append(QJsonArray{100.0, 200.0, 50.0, 60.0}); // no label
    root.insert("boxes", boxes);
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "fixture written");
    file.write(QJsonDocument(root).toJson());
    file.close();

    mviewer::ui::CvOverlay overlay;
    CHECK(overlay.isEmpty() && overlay.count() == 0, "a fresh overlay is empty");

    QString error;
    CHECK(overlay.loadFromJsonFile(path, &error), "the sidecar loads");
    CHECK(overlay.count() == 2, "degenerate and truncated entries are dropped");
    CHECK(overlay.isEmpty() == false, "a loaded overlay is not empty");

    overlay.clear();
    CHECK(overlay.isEmpty(), "clear empties the overlay");

    CHECK(!overlay.loadFromJsonFile(tmp.filePath("absent.json"), &error) && !error.isEmpty(),
          "a missing sidecar reports a reason");

    if (g_failures != 0)
    {
        std::cout << "cv_overlay_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "cv_overlay_tests: PASS\n";
    return 0;
}
