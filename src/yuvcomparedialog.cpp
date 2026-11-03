#include "yuvcomparedialog.h"

#include <QAbstractItemView>
#include <QClipboard>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{

constexpr int kPollMs = 100;

QStringList metricCells(const mviewer::core::yuv::YuvFrameMetrics &m)
{
    const bool gray = m.planeCount < 3;
    const QString dash = QStringLiteral("-");
    QStringList cells;
    cells << YuvCompareDialog::psnrText(m.planes[0].psnr);
    cells << (gray ? dash : YuvCompareDialog::psnrText(m.planes[1].psnr));
    cells << (gray ? dash : YuvCompareDialog::psnrText(m.planes[2].psnr));
    cells << YuvCompareDialog::psnrText(m.psnr611);
    cells << QString::number(m.planes[0].ssim, 'f', 4);
    cells << (gray ? dash : QString::number(m.planes[1].ssim, 'f', 4));
    cells << (gray ? dash : QString::number(m.planes[2].ssim, 'f', 4));
    cells << QString::number(m.ssim611, 'f', 4);
    return cells;
}

void writeRow(QTableWidget *table, int row, const QString &label, const QStringList &cells,
              bool bold)
{
    auto *head = new QTableWidgetItem(label);
    if (bold)
    {
        QFont font = head->font();
        font.setBold(true);
        head->setFont(font);
    }
    table->setItem(row, 0, head);
    const int cellCount = static_cast<int>(cells.size());
    for (int column = 0; column < cellCount; ++column)
    {
        auto *item = new QTableWidgetItem(cells.at(column));
        if (bold)
        {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
        }
        table->setItem(row, column + 1, item);
    }
}

QStringList columnTitles()
{
    return {YuvCompareDialog::tr("Frame"),      YuvCompareDialog::tr("Y PSNR"),
            YuvCompareDialog::tr("U PSNR"),     YuvCompareDialog::tr("V PSNR"),
            YuvCompareDialog::tr("6:1:1 PSNR"), YuvCompareDialog::tr("Y SSIM"),
            YuvCompareDialog::tr("U SSIM"),     YuvCompareDialog::tr("V SSIM"),
            YuvCompareDialog::tr("6:1:1 SSIM")};
}

} // namespace

YuvCompareDialog::YuvCompareDialog(const QString &pathA, const QString &pathB, QWidget *parent)
    : QDialog(parent), m_pathA(pathA), m_pathB(pathB)
{
    m_nameA = QFileInfo(pathA).fileName();
    m_nameB = QFileInfo(pathB).fileName();
    buildUi(pathA, pathB);
}

YuvCompareDialog::~YuvCompareDialog()
{
    m_cancel.store(true);
    if (m_timer)
    {
        m_timer->stop();
    }
    if (m_worker.joinable())
    {
        m_worker.join();
    }
}

void YuvCompareDialog::buildUi(const QString &pathA, const QString &pathB)
{
    setWindowTitle(tr("Compare YUV"));
    m_paths = new QLabel(tr("A: %1").arg(QFileInfo(pathA).fileName()) + QLatin1Char('\n') +
                             tr("B: %1").arg(QFileInfo(pathB).fileName()),
                         this);
    m_paths->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status = new QLabel(tr("Comparing..."), this);
    m_status->setWordWrap(true);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_table = new QTableWidget(this);
    const QStringList titles = columnTitles();
    m_table->setColumnCount(static_cast<int>(titles.size()));
    m_table->setHorizontalHeaderLabels(titles);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);

    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_copyButton = new QPushButton(tr("Copy"), this);
    m_closeButton = new QPushButton(tr("Close"), this);
    m_copyButton->setEnabled(false);
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() { m_cancel.store(true); });
    connect(m_copyButton, &QPushButton::clicked, this,
            [this]()
            {
                if (QGuiApplication::clipboard())
                {
                    QGuiApplication::clipboard()->setText(resultText());
                }
            });
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::close);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_cancelButton);
    buttons->addWidget(m_copyButton);
    buttons->addStretch(1);
    buttons->addWidget(m_closeButton);

    auto *layout = new QVBoxLayout;
    layout->addWidget(m_paths);
    layout->addWidget(m_status);
    layout->addWidget(m_progress);
    layout->addWidget(m_table, 1);
    layout->addLayout(buttons);
    setLayout(layout);
    resize(860, 480);

    m_timer = new QTimer(this);
    m_timer->setInterval(kPollMs);
    connect(m_timer, &QTimer::timeout, this, &YuvCompareDialog::poll);
}

void YuvCompareDialog::start()
{
    if (m_started.exchange(true))
    {
        return;
    }
    const std::string pathA = m_pathA.toUtf8().toStdString();
    const std::string pathB = m_pathB.toUtf8().toStdString();
    m_timer->start();
    m_worker = std::thread(
        [this, pathA, pathB]()
        {
            const mviewer::core::yuv::YuvSequenceMetrics metrics =
                mviewer::core::yuv::compareYuvFiles(pathA, pathB, 0,
                                                    [this](int64_t done, int64_t total)
                                                    {
                                                        m_doneFrames.store(done);
                                                        m_totalFrames.store(total);
                                                        return !m_cancel.load();
                                                    });
            {
                const std::lock_guard<std::mutex> lock(m_metricsMutex);
                m_metrics = metrics;
            }
            m_done.store(true);
        });
}

bool YuvCompareDialog::isFinished() const
{
    return m_finished;
}

QString YuvCompareDialog::resultText() const
{
    return m_resultText;
}

QString YuvCompareDialog::psnrText(double db)
{
    if (std::isinf(db))
    {
        return QStringLiteral("inf");
    }
    return QString::number(db, 'f', 2);
}

QString YuvCompareDialog::frameRowText(int64_t index, const mviewer::core::yuv::YuvFrameMetrics &m)
{
    return QString::number(index) + QLatin1Char('\t') + metricCells(m).join(QLatin1Char('\t'));
}

void YuvCompareDialog::updateProgress(int64_t done, int64_t total)
{
    const int64_t limit = static_cast<int64_t>(std::numeric_limits<int>::max());
    if (total <= 0 || total > limit)
    {
        m_progress->setRange(0, 0);
        return;
    }
    const int64_t clamped = std::min(done, total);
    m_progress->setRange(0, static_cast<int>(total));
    m_progress->setValue(static_cast<int>(clamped));
}

void YuvCompareDialog::poll()
{
    updateProgress(m_doneFrames.load(), m_totalFrames.load());
    if (!m_done.load())
    {
        return;
    }
    m_timer->stop();
    if (m_worker.joinable())
    {
        m_worker.join();
    }
    populateResult();
}

void YuvCompareDialog::showFailure(const mviewer::core::yuv::YuvSequenceMetrics &metrics)
{
    if (metrics.why == "cancelled")
    {
        m_status->setText(tr("Cancelled."));
    }
    else
    {
        m_status->setText(tr("Cannot compare as YUV: %1").arg(QString::fromStdString(metrics.why)) +
                          QLatin1Char('\n') +
                          tr("Use the normal compare view for an RGB comparison."));
    }
    m_resultText = m_status->text();
    m_copyButton->setEnabled(false);
    m_cancelButton->setEnabled(false);
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_finished = true;
}

void YuvCompareDialog::showSuccess(const mviewer::core::yuv::YuvSequenceMetrics &metrics)
{
    m_status->setText(tr("%1 frames compared (A has %2, B has %3)")
                          .arg(metrics.framesCompared)
                          .arg(metrics.framesA)
                          .arg(metrics.framesB));
    QString text = tr("A: %1").arg(m_nameA) + QLatin1Char('\n') + tr("B: %1").arg(m_nameB) +
                   QLatin1Char('\n') + m_status->text() + QLatin1Char('\n');
    text += columnTitles().join(QLatin1Char('\t')) + QLatin1Char('\n');
    text += tr("Mean") + QLatin1Char('\t') + metricCells(metrics.mean).join(QLatin1Char('\t')) +
            QLatin1Char('\n');
    for (int64_t index = 0; index < static_cast<int64_t>(metrics.frames.size()); ++index)
    {
        text += frameRowText(index, metrics.frames[static_cast<size_t>(index)]) + QLatin1Char('\n');
    }
    m_resultText = text;
    fillTable(metrics);
    m_copyButton->setEnabled(true);
    m_cancelButton->setEnabled(false);
    m_finished = true;
}

void YuvCompareDialog::fillTable(const mviewer::core::yuv::YuvSequenceMetrics &metrics)
{
    const int frameCount = static_cast<int>(metrics.frames.size());
    m_table->setRowCount(frameCount + 1);
    writeRow(m_table, 0, tr("Mean"), metricCells(metrics.mean), true);
    for (int row = 0; row < frameCount; ++row)
    {
        const auto index = static_cast<size_t>(row);
        writeRow(m_table, row + 1, QString::number(row), metricCells(metrics.frames[index]), false);
    }
}

void YuvCompareDialog::populateResult()
{
    mviewer::core::yuv::YuvSequenceMetrics metrics;
    {
        const std::lock_guard<std::mutex> lock(m_metricsMutex);
        metrics = m_metrics;
    }
    if (!metrics.valid)
    {
        showFailure(metrics);
        return;
    }
    showSuccess(metrics);
}

YuvCompareDialog *runYuvCompare(QWidget *parent, const QString &pathA, const QString &pathB)
{
    auto *dialog = new YuvCompareDialog(pathA, pathB, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->start();
    return dialog;
}
