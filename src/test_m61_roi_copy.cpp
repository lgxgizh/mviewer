// M61 follow-up: Compare Ctrl+C / Ctrl+S is WYSIWYG. The per-pane ROI
// statistics chips float on the workspace (above the Grid page / Canvas), so a
// copy that grabbed only the page silently dropped the numbers the user sees.
#include "compareworkspace.h"
#include "core/scheduler/TaskScheduler.h"
#include "widgets/rawimageview.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace
{
int g_failures = 0;

#define CHECK(condition, message)                                                                  \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            std::printf("FAIL: %s\n", message);                                                    \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

void pump(int ms = 30)
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
        pump(20);
    return predicate();
}

QString writePattern(const QDir &dir, const QString &name, int bias)
{
    QImage image(96, 72, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(
                x, y,
                QColor((x * 7 + bias) & 0xff, (y * 11 + 20) & 0xff, (x * 3 + y * 5 + 40) & 0xff));
    const QString path = dir.filePath(name);
    image.save(path, "PNG");
    return path;
}

RawImageView *pane(CompareWorkspace *workspace, int index)
{
    for (RawImageView *view : workspace->findChildren<RawImageView *>())
        if (view->cellIndex() == index)
            return view;
    return nullptr;
}

QCheckBox *findCheckBox(QWidget *root, const QString &prefix)
{
    for (QCheckBox *box : root->findChildren<QCheckBox *>())
        if (box->text().startsWith(prefix))
            return box;
    return nullptr;
}

void rightDrag(QWidget *target, const QPoint &from, const QPoint &to)
{
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(from), target->mapToGlobal(from),
                      Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(to), target->mapToGlobal(to), Qt::NoButton,
                     Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(target, &move);
    pump(10);
    QMouseEvent up(QEvent::MouseButtonRelease, QPointF(to), target->mapToGlobal(to),
                   Qt::RightButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(target, &up);
    pump(10);
}

QImage copyView(CompareWorkspace *workspace)
{
    QApplication::clipboard()->clear();
    QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    QApplication::sendEvent(workspace, &copy);
    return QApplication::clipboard()->image().convertToFormat(QImage::Format_RGB32);
}

bool chipReady(const QLabel *chip)
{
    return chip && chip->isVisible() && chip->text().contains(QStringLiteral("R/G")) &&
           !chip->text().contains(QStringLiteral("…"));
}

// The copied image must carry each visible chip at its on-screen position:
// the chip's pixels differ from a copy taken with the chips hidden, and its
// #FFD233 border is present there.
void checkChipsInCopy(CompareWorkspace *workspace, QWidget *page, const char *mode)
{
    QList<QLabel *> chips;
    for (int index = 0; index < 2; ++index)
    {
        auto *chip = workspace->findChild<QLabel *>(QStringLiteral("roiPaneChip%1").arg(index));
        if (chipReady(chip))
            chips.push_back(chip);
    }
    char message[160];
    std::snprintf(message, sizeof(message), "%s: ROI statistics chips are visible", mode);
    CHECK(!chips.isEmpty(), message);
    if (chips.isEmpty() || !page)
        return;

    const QImage withChips = copyView(workspace);
    std::snprintf(message, sizeof(message), "%s: Ctrl+C puts an image on the clipboard", mode);
    CHECK(!withChips.isNull(), message);
    if (withChips.isNull())
        return;
    const double scale = double(withChips.width()) / double(page->width());
    std::snprintf(message, sizeof(message), "%s: copy covers the visible compare page", mode);
    CHECK(scale > 0.5 && withChips.height() > int(page->height() * scale), message);

    for (QLabel *chip : chips)
        chip->hide();
    const QImage withoutChips = copyView(workspace);
    for (QLabel *chip : chips)
        chip->show();
    pump(10);
    if (withoutChips.size() != withChips.size())
    {
        CHECK(false, "copy size is stable while toggling chips");
        return;
    }

    for (QLabel *chip : chips)
    {
        const QRect onPage(chip->mapTo(page, QPoint(0, 0)), chip->size());
        const QRect inCopy(int(onPage.x() * scale), int(onPage.y() * scale),
                           int(onPage.width() * scale), int(onPage.height() * scale));
        const QRect clipped = inCopy & QRect(0, 0, withChips.width(), int(page->height() * scale));
        int differing = 0;
        int border = 0;
        for (int y = clipped.top(); y <= clipped.bottom(); ++y)
            for (int x = clipped.left(); x <= clipped.right(); ++x)
            {
                const QRgb a = withChips.pixel(x, y);
                const QRgb b = withoutChips.pixel(x, y);
                if (std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) +
                        std::abs(qBlue(a) - qBlue(b)) >
                    24)
                    ++differing;
                if (qRed(a) > 220 && qGreen(a) > 180 && qGreen(a) < 240 && qBlue(a) < 110)
                    ++border;
            }
        const int area = std::max(1, clipped.width() * clipped.height());
        std::snprintf(message, sizeof(message),
                      "%s: %s pixels appear in the copied image (%d/%d differ)", mode,
                      qPrintable(chip->objectName()), differing, area);
        CHECK(differing * 10 >= area, message);
        std::snprintf(message, sizeof(message), "%s: %s border is in the copied image (%d px)",
                      mode, qPrintable(chip->objectName()), border);
        CHECK(border >= clipped.width(), message);
    }
}
} // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    CHECK(temporary.isValid(), "temporary directory is available");
    if (!temporary.isValid())
        return 1;
    const QDir dir(temporary.path());
    const QString a = writePattern(dir, QStringLiteral("copy-A.png"), 10);
    const QString b = writePattern(dir, QStringLiteral("copy-B.png"), 60);

    QDialog dialog;
    auto *layout = new QVBoxLayout(&dialog);
    auto *workspace = new CompareWorkspace(&dialog);
    layout->addWidget(workspace);
    workspace->setImages({a, b});
    dialog.resize(1100, 760);
    dialog.show();
    CHECK(waitFor([&]() { return workspace->comparedImageCount() == 2; }), "two panes load");
    const bool panesReady = waitFor(
        [&]()
        {
            RawImageView *candidate = pane(workspace, 0);
            return candidate && candidate->sourceSize() == QSize(96, 72);
        });
    RawImageView *first = pane(workspace, 0);
    CHECK(panesReady && first, "Grid panes expose source geometry");
    if (!panesReady || !first)
        return 1;

    // Grid: right-drag an ROI and wait for the real asynchronous stats.
    rightDrag(first, first->sourcePointToWidget(QPointF(12, 10)).toPoint(),
              first->sourcePointToWidget(QPointF(40, 32)).toPoint());
    auto *table = workspace->findChild<QTableWidget *>("roiMeasurementTable");
    CHECK(table && waitFor([&]() { return table->rowCount() == 2; }), "Grid ROI is measured");
    CHECK(waitFor([&]() { return chipReady(workspace->findChild<QLabel *>("roiPaneChip0")); }),
          "Grid ROI chip shows numbers");
    pump(50);
    checkChipsInCopy(workspace, workspace->findChild<QWidget *>("compareGridPage"), "Grid");

    // Split canvas: the chips sit over the canvas page.
    QCheckBox *split = findCheckBox(workspace, QStringLiteral("Split"));
    QWidget *canvas = workspace->findChild<QWidget *>("compareCanvas");
    CHECK(split && canvas, "Split mode and canvas exist");
    if (split && canvas)
    {
        split->setChecked(true);
        pump(80);
        canvas->repaint();
        const QPoint from(canvas->width() / 8, canvas->height() / 3);
        rightDrag(canvas, from, from + QPoint(60, 45));
        CHECK(!workspace->currentROI().isEmpty(), "Split ROI exists");
        CHECK(waitFor([&]() { return chipReady(workspace->findChild<QLabel *>("roiPaneChip0")); }),
              "Split ROI chip shows numbers");
        pump(50);
        checkChipsInCopy(workspace, canvas, "Split");
        split->setChecked(false);
        pump(20);
    }

    TaskScheduler::instance().drain(TaskScheduler::AnalysisPool, std::chrono::seconds(5));
    std::printf("M61 ROI copy failures: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
