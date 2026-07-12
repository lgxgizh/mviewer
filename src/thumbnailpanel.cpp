#include "thumbnailpanel.h"

#include <algorithm>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>

#include "thumbnailcache.h"

namespace
{
const QStringList kImageExtensions = {"*.jpg", "*.jpeg", "*.bmp", "*.png"};

QList<QFileInfo> sortedEntries(const QString &dirPath,
                               ThumbnailPanel::SortMode mode)
{
    QDir dir(dirPath);
    if (!dir.exists())
        return {};

    QFileInfoList entries =
        dir.entryInfoList(kImageExtensions, QDir::Files);
    if (entries.isEmpty())
        return {};

    switch (mode) {
    case ThumbnailPanel::SortName:
        std::sort(entries.begin(), entries.end(),
                  [](const QFileInfo &a, const QFileInfo &b) {
                      return a.fileName().compare(b.fileName(),
                                                  Qt::CaseInsensitive) < 0;
                  });
        break;
    case ThumbnailPanel::SortDate:
        std::sort(entries.begin(), entries.end(),
                  [](const QFileInfo &a, const QFileInfo &b) {
                      return a.lastModified() < b.lastModified();
                  });
        break;
    case ThumbnailPanel::SortSize:
        std::sort(entries.begin(), entries.end(),
                  [](const QFileInfo &a, const QFileInfo &b) {
                      return a.size() < b.size();
                  });
        break;
    case ThumbnailPanel::SortResolution: {
        auto area = [](const QFileInfo &fi) {
            const QImageReader r(fi.absoluteFilePath());
            return r.size().width() * r.size().height();
        };
        std::sort(entries.begin(), entries.end(),
                  [&](const QFileInfo &a, const QFileInfo &b) {
                      return area(a) < area(b);
                  });
        break;
    }
    }
    return entries;
}
} // namespace

// ---- ThumbnailWorker -------------------------------------------------------

ThumbnailWorker::ThumbnailWorker(QObject *parent)
    : QObject(parent)
{
}

void ThumbnailWorker::stop()
{
    QMutexLocker lock(&m_mutex);
    m_stop = true;
    m_cond.wakeAll();
}

void ThumbnailWorker::enqueue(const Request &req)
{
    QMutexLocker lock(&m_mutex);
    // De-duplicate: keep only the latest request for the same id.
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue[i].id == req.id) {
            m_queue[i] = req;
            m_cond.wakeOne();
            return;
        }
    }
    m_queue.enqueue(req);
    m_cond.wakeOne();
}

QPixmap ThumbnailWorker::makeThumbnail(const QString &path)
{
    // On-disk cache first.
    QPixmap cached;
    if (ThumbnailCache::instance().get(path, cached))
        return cached;

    // Decode at thumbnail resolution (avoids full-image decode).
    QImageReader reader(path);
    reader.setAutoDetectImageFormat(true);
    if (!reader.canRead())
        return {};

    const QSize full = reader.size();
    if (!full.isValid() || full.isEmpty())
        return {};

    // Only downscale if the image is larger than the thumb size.
    int target = ThumbnailPanel::kThumbSize;
    if (full.width() > target || full.height() > target) {
        const double ratio = static_cast<double>(target) /
                             std::max(full.width(), full.height());
        reader.setScaledSize(
            QSize(static_cast<int>(full.width() * ratio),
                  static_cast<int>(full.height() * ratio)));
    }

    QImage img = reader.read();
    if (img.isNull())
        return {};

    // Compose on a square transparent canvas with aspect-correct scaling.
    QPixmap pm(ThumbnailPanel::kThumbSize, ThumbnailPanel::kThumbSize);
    pm.fill(Qt::transparent);
    QPixmap scaled = QPixmap::fromImage(img).scaled(
        ThumbnailPanel::kThumbSize, ThumbnailPanel::kThumbSize, Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    QPainter painter(&pm);
    painter.drawPixmap((ThumbnailPanel::kThumbSize - scaled.width()) / 2,
                       (ThumbnailPanel::kThumbSize - scaled.height()) / 2, scaled);
    painter.end();

    ThumbnailCache::instance().put(path, pm);
    return pm;
}

// Worker event loop (runs on the worker thread via exec()).
void ThumbnailWorker::process()
{
    for (;;) {
        Request req;
        {
            QMutexLocker lock(&m_mutex);
            while (m_queue.isEmpty() && !m_stop)
                m_cond.wait(&m_mutex);
            if (m_stop)
                break;
            if (m_queue.isEmpty())
                continue;
            req = m_queue.dequeue();
        }
        const QPixmap pm = makeThumbnail(req.path);
        if (!pm.isNull())
            emit thumbnailReady(req.path, pm, req.id);
    }
    emit finished();
}

// ---- ThumbnailPanel --------------------------------------------------------

ThumbnailPanel::ThumbnailPanel(QWidget *parent)
    : QListWidget(parent)
{
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setIconSize(QSize(kThumbSize, kThumbSize));
    setGridSize(QSize(kThumbSize + 16, kThumbSize + 30));
    setSpacing(8);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::SingleSelection);

    startWorker();

    connect(this, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
                if (item)
                    emit itemClicked(item->data(Qt::UserRole).toString());
            });
    connect(this, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) {
                if (item)
                    emit itemDoubleClicked(
                        item->data(Qt::UserRole).toString());
            });

    connect(m_worker, &ThumbnailWorker::thumbnailReady, this,
            [this](const QString &path, const QPixmap &pm,
                   const QString &id) {
                // Match by the stored id to avoid stale updates after
                // a directory switch.
                Q_UNUSED(path);
                QListWidgetItem *item = m_itemById.value(id, nullptr);
                if (item)
                    item->setIcon(QIcon(pm));
            },
            Qt::QueuedConnection);
}

ThumbnailPanel::~ThumbnailPanel()
{
    stopWorker();
}

void ThumbnailPanel::startWorker()
{
    m_worker = new ThumbnailWorker;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker,
            &QObject::deleteLater);
    m_thread.start();
    QMetaObject::invokeMethod(m_worker, &ThumbnailWorker::process,
                              Qt::QueuedConnection);
}

void ThumbnailPanel::stopWorker()
{
    if (m_worker)
        m_worker->stop();
    m_thread.quit();
    m_thread.wait();
    m_worker = nullptr;
}

void ThumbnailPanel::setSortMode(SortMode mode)
{
    if (mode == m_sortMode)
        return;
    m_sortMode = mode;
    if (!m_currentDir.isEmpty())
        setDirectory(m_currentDir);
}

void ThumbnailPanel::setDirectory(const QString &path)
{
    m_currentDir = path;
    clear();
    m_itemById.clear();

    const QList<QFileInfo> entries = sortedEntries(path, m_sortMode);
    for (int i = 0; i < entries.size() && i < kMaxImages; ++i) {
        const QFileInfo &info = entries.at(i);
        const QString filePath = info.absoluteFilePath();
        const QString id = path + "#" + QString::number(i);

        auto *item = new QListWidgetItem(this);
        item->setData(Qt::UserRole, filePath);
        item->setData(Qt::UserRole + 1, id);
        item->setText(info.fileName());
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
        // Placeholder icon until the worker delivers the real thumbnail.
        addItem(item);
        m_itemById.insert(id, item);

        ThumbnailWorker::Request req{filePath, kThumbSize, id};
        m_worker->enqueue(req);
    }
}
