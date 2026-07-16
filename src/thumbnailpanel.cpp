#include "thumbnailpanel.h"

#include "application/DeleteImageUseCase.h"
#include "application/RenameImageUseCase.h"
#include "core/EventBus.h"

#include "core/image/Decoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageRepository.h"
#include "core/image/QtConvert.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "thumbnailcache.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <algorithm>

namespace
{
const QStringList kImageExtensions = {"*.jpg", "*.jpeg", "*.bmp", "*.png", "*.tif", "*.tiff"};

QList<QFileInfo> sortedEntries(const QString &dirPath, ThumbnailPanel::SortMode mode)
{
    QDir dir(dirPath);
    if (!dir.exists())
        return {};

    QFileInfoList entries = dir.entryInfoList(kImageExtensions, QDir::Files);
    if (entries.isEmpty())
        return {};

    switch (mode)
    {
    case ThumbnailPanel::SortName:
        std::sort(entries.begin(), entries.end(),
                  [](const QFileInfo &a, const QFileInfo &b)
                  { return a.fileName().compare(b.fileName(), Qt::CaseInsensitive) < 0; });
        break;
    case ThumbnailPanel::SortDate:
        std::sort(entries.begin(), entries.end(),
                  [](const QFileInfo &a, const QFileInfo &b)
                  { return a.lastModified() < b.lastModified(); });
        break;
    case ThumbnailPanel::SortSize:
        std::sort(entries.begin(), entries.end(),
                  [](const QFileInfo &a, const QFileInfo &b) { return a.size() < b.size(); });
        break;
    case ThumbnailPanel::SortResolution:
    {
        auto area = [](const QFileInfo &fi)
        {
            const auto meta =
                ImageRepository::instance().metadata(fi.absoluteFilePath().toStdString());
            return static_cast<qint64>(meta.width) * meta.height;
        };
        std::sort(entries.begin(), entries.end(),
                  [&](const QFileInfo &a, const QFileInfo &b) { return area(a) < area(b); });
        break;
    }
    }
    return entries;
}
} // namespace

// ---- ThumbnailWorker -------------------------------------------------------

ThumbnailWorker::ThumbnailWorker(QObject *parent) : QObject(parent)
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
    for (int i = 0; i < m_queue.size(); ++i)
    {
        if (m_queue[i].id == req.id)
        {
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
    // 1) On-disk cache (persistent tier).
    QPixmap cached;
    if (ThumbnailCache::instance().get(path, cached))
        return cached;

    // 2) ThumbnailPipeline in-memory LRU (hot tier). The pipeline decodes on a
    //    background scheduler thread with visible-range priority + predictive
    //    loading; here we probe its cache synchronously. If present, use it.
    ImageData memHit = ThumbnailPipeline::instance().request(path.toStdString());
    if (!memHit.isNull())
    {
        QImage qimg = mvcore::toQImage(memHit);
        if (!qimg.isNull())
        {
            QPixmap pm(ThumbnailPanel::kThumbSize, ThumbnailPanel::kThumbSize);
            pm.fill(Qt::transparent);
            QPixmap scaled = QPixmap::fromImage(qimg).scaled(
                ThumbnailPanel::kThumbSize, ThumbnailPanel::kThumbSize, Qt::KeepAspectRatio,
                Qt::SmoothTransformation);
            QPainter painter(&pm);
            painter.drawPixmap((ThumbnailPanel::kThumbSize - scaled.width()) / 2,
                               (ThumbnailPanel::kThumbSize - scaled.height()) / 2, scaled);
            painter.end();
            return pm;
        }
    }

    // 3) Decode at thumbnail resolution through the core decoder (no UI-layer
    //    decode). Store into the pipeline LRU for fast reuse on revisit.
    ImageData img = Decoder::decodeScaled(path.toStdString(), ThumbnailPanel::kThumbSize);
    if (img.isNull())
        return {};
    QImage qimg = mvcore::toQImage(img);
    if (qimg.isNull())
        return {};

    // Compose on a square transparent canvas with aspect-correct scaling.
    QPixmap pm(ThumbnailPanel::kThumbSize, ThumbnailPanel::kThumbSize);
    pm.fill(Qt::transparent);
    QPixmap scaled =
        QPixmap::fromImage(qimg).scaled(ThumbnailPanel::kThumbSize, ThumbnailPanel::kThumbSize,
                                        Qt::KeepAspectRatio, Qt::SmoothTransformation);
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
    for (;;)
    {
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

ThumbnailPanel::ThumbnailPanel(QWidget *parent) : QListWidget(parent)
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
    setSelectionMode(QAbstractItemView::MultiSelection);

    startWorker();

    m_compareBtn = new QPushButton("比较(&C)", this);
    m_compareBtn->setVisible(true);
    connect(m_compareBtn, &QPushButton::clicked, this, &ThumbnailPanel::onCompareClicked);

    connect(this, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item)
            {
                if (item)
                    emit itemClicked(item->data(Qt::UserRole).toString());
            });
    connect(this, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item)
            {
                if (item)
                {
                    const QString path = item->data(Qt::UserRole).toString();
                    emit itemDoubleClicked(path);
                    EventBus::instance().publish("image.open", const_cast<QString *>(&path));
                }
            });

    connect(
        m_worker, &ThumbnailWorker::thumbnailReady, this,
        [this](const QString &path, const QPixmap &pm, const QString &id)
        {
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
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
    QMetaObject::invokeMethod(m_worker, &ThumbnailWorker::process, Qt::QueuedConnection);
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
    for (int i = 0; i < entries.size() && i < kMaxImages; ++i)
    {
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

QStringList ThumbnailPanel::selectedPaths() const
{
    QStringList result;
    for (QListWidgetItem *item : selectedItems())
        result.append(item->data(Qt::UserRole).toString());
    return result;
}

void ThumbnailPanel::onCompareClicked()
{
    const QStringList sel = selectedPaths();
    if (sel.size() < 2 || sel.size() > 8)
    {
        QMessageBox::warning(this, "比较模式", "请选择 2~8 张图片");
        return;
    }
    emit compareRequested(sel);
    EventBus::instance().publish("compare.requested", const_cast<QStringList *>(&sel));
}

void ThumbnailPanel::renameSelected()
{
    const QList<QListWidgetItem *> items = selectedItems();
    if (items.isEmpty())
        return;

    const QString oldPath = items.first()->data(Qt::UserRole).toString();
    const QFileInfo fi(oldPath);
    const QString newName = QInputDialog::getText(this, "重命名", "请输入新的文件名：",
                                                  QLineEdit::Normal, fi.fileName());
    if (newName.isEmpty() || newName == fi.fileName())
        return;
    // 不接受带路径分隔符的输入(只改文件名)
    if (newName.contains('/') || newName.contains('\\'))
    {
        QMessageBox::warning(this, "重命名", "文件名不能包含路径分隔符");
        return;
    }

    const QString newPath = fi.absolutePath() + "/" + newName;
    RenameImageUseCase::Result r =
        RenameImageUseCase::execute(oldPath.toStdString(), newName.toStdString());
    if (r.success)
        setDirectory(m_currentDir);
    else
        QMessageBox::warning(this, "重命名", "重命名失败");
}

void ThumbnailPanel::moveToTrashSelected()
{
    const QStringList sel = selectedPaths();
    if (sel.isEmpty())
        return;

    const QString msg =
        sel.size() == 1
            ? QString("确定将 \"%1\" 移入回收站吗？").arg(QFileInfo(sel.first()).fileName())
            : QString("确定将 %1 个文件移入回收站吗？").arg(sel.size());
    if (QMessageBox::question(this, "删除到回收站", msg, QMessageBox::Yes | QMessageBox::No) !=
        QMessageBox::Yes)
        return;

    bool allOk = true;
    for (const QString &path : sel)
    {
        if (!DeleteImageUseCase::execute(path.toStdString()).success)
            allOk = false;
    }
    if (allOk)
        setDirectory(m_currentDir);
    else
        QMessageBox::warning(this, "删除到回收站", "部分文件删除失败");
}

void ThumbnailPanel::contextMenuEvent(QContextMenuEvent *event)
{
    QListWidgetItem *item = itemAt(event->pos());
    if (!item)
        return;

    // 确保右键项被选中,便于后续操作
    if (!item->isSelected())
        setCurrentItem(item, QItemSelectionModel::Select);

    QMenu menu(this);
    QAction *actRename = menu.addAction("重命名(&R)");
    QAction *actTrash = menu.addAction("删除到回收站(&D)");
    connect(actRename, &QAction::triggered, this, &ThumbnailPanel::renameSelected);
    connect(actTrash, &QAction::triggered, this, &ThumbnailPanel::moveToTrashSelected);
    menu.exec(event->globalPos());
}

void ThumbnailPanel::resizeEvent(QResizeEvent *event)
{
    QListWidget::resizeEvent(event);
    if (m_compareBtn)
    {
        const int bw = 90;
        const int bh = 28;
        const int margin = 6;
        m_compareBtn->setGeometry(width() - bw - margin, height() - bh - margin, bw, bh);
        m_compareBtn->raise();
    }
}
