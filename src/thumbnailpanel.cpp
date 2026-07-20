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
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QUrl>
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
        std::sort(entries.begin(), entries.end(), [](const QFileInfo &a, const QFileInfo &b)
                  { return a.fileName().compare(b.fileName(), Qt::CaseInsensitive) < 0; });
        break;
    case ThumbnailPanel::SortDate:
        std::sort(entries.begin(), entries.end(), [](const QFileInfo &a, const QFileInfo &b)
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

void ThumbnailPanel::stopThumbnailWorker()
{
    stopWorker();
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
    // M18: drop any recursive-search temp items + reset the active filter state
    // so a directory switch starts from a clean, unfiltered view.
    qDeleteAll(m_recursiveItems);
    m_recursiveItems.clear();
    m_filterText.clear();
    m_filterRecursive = false;

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

void ThumbnailPanel::scrollToPath(const QString &path)
{
    if (path.isEmpty())
        return;
    // Find the item by its stored absolute path (UserRole).
    for (int i = 0; i < count(); ++i)
    {
        QListWidgetItem *it = item(i);
        if (it && it->data(Qt::UserRole).toString() == path)
        {
            setCurrentItem(it);
            scrollToItem(it, QAbstractItemView::PositionAtCenter);
            return;
        }
    }
}

int ThumbnailPanel::scrollOffset() const
{
    QScrollBar *bar = verticalScrollBar();
    return bar ? bar->value() : 0;
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

namespace
{
// Collect image files under `root` whose filename contains `needle`
// (case-insensitive). Recurses into subfolders.
QStringList recursiveMatches(const QString &root, const QString &needle)
{
    QStringList out;
    QDir dir(root);
    if (!dir.exists())
        return out;
    const QDir::Filters filters = QDir::Files | QDir::Readable;
    QFileInfoList top = dir.entryInfoList(kImageExtensions, filters);
    for (const QFileInfo &fi : top)
        if (fi.fileName().contains(needle, Qt::CaseInsensitive))
            out.append(fi.absoluteFilePath());
    // Recurse one level at a time (bounded) to avoid pathological deep trees.
    QFileInfoList sub = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
    for (const QFileInfo &d : sub)
    {
        if (out.size() >= ThumbnailPanel::kMaxImages)
            break;
        out.append(recursiveMatches(d.absoluteFilePath(), needle));
    }
    return out;
}
} // namespace

void ThumbnailPanel::setFilter(const QString &text, bool recursive)
{
    m_filterText = text.trimmed();
    m_filterRecursive = recursive;

    // Reset to the base directory listing, then apply the filter.
    const QString baseDir = m_currentDir;
    setDirectory(baseDir); // clears + reloads (also clears this same filter state)
    // setDirectory() reset m_filterText; restore it so the live search sticks.
    m_filterText = text.trimmed();
    m_filterRecursive = recursive;

    if (m_filterText.isEmpty())
        return;

    // 1) Hide non-matching items currently in the directory.
    for (int i = 0; i < count(); ++i)
    {
        QListWidgetItem *it = item(i);
        if (!it)
            continue;
        const QString name = QFileInfo(it->data(Qt::UserRole).toString()).fileName();
        it->setHidden(!name.contains(m_filterText, Qt::CaseInsensitive));
    }

    // 2) Recursive: append matches found in subfolders as extra items.
    if (recursive)
    {
        const QStringList matches = recursiveMatches(baseDir, m_filterText);
        int idx = count();
        for (const QString &p : matches)
        {
            if (idx >= kMaxImages)
                break;
            const QFileInfo fi(p);
            auto *it = new QListWidgetItem(this);
            it->setData(Qt::UserRole, p);
            it->setData(Qt::UserRole + 1, p + "#rec");
            it->setText(fi.fileName() + "  [" + fi.dir().dirName() + "]");
            addItem(it);
            m_recursiveItems.append(it);
            ThumbnailWorker::Request req{p, kThumbSize, p + "#rec"};
            m_worker->enqueue(req);
            ++idx;
        }
    }
}

void ThumbnailPanel::copySelectedTo()
{
    const QStringList sel = selectedPaths();
    if (sel.isEmpty())
        return;
    const QString dest = QFileDialog::getExistingDirectory(this, tr("复制到..."), m_currentDir);
    if (dest.isEmpty())
        return;
    int copied = 0;
    for (const QString &p : sel)
    {
        const QFileInfo fi(p);
        if (QFile::copy(p, dest + "/" + fi.fileName()))
            ++copied;
    }
    QMessageBox::information(this, tr("复制完成"),
                             tr("已复制 %1 / %2 个文件到:\n%3").arg(copied).arg(sel.size()).arg(dest));
}

void ThumbnailPanel::moveSelectedTo()
{
    const QStringList sel = selectedPaths();
    if (sel.isEmpty())
        return;
    const QString dest = QFileDialog::getExistingDirectory(this, tr("移动到..."), m_currentDir);
    if (dest.isEmpty())
        return;
    int moved = 0;
    for (const QString &p : sel)
    {
        const QFileInfo fi(p);
        if (QFile::rename(p, dest + "/" + fi.fileName()))
            ++moved;
    }
    if (moved > 0)
        setDirectory(m_currentDir);
    QMessageBox::information(this, tr("移动完成"),
                             tr("已移动 %1 / %2 个文件到:\n%3").arg(moved).arg(sel.size()).arg(dest));
}

void ThumbnailPanel::revealSelected()
{
    const QStringList sel = selectedPaths();
    if (sel.isEmpty())
        return;
    // Open the OS file manager with the first selected file highlighted.
    // Explanatory note: on Windows, explorer /select, passes the file path.
    const QString first = sel.first();
    const QUrl url = QUrl::fromLocalFile(first);
#ifdef Q_OS_WIN
    // Use explorer's /select switch to highlight the file in its folder.
    QProcess::startDetached("explorer", QStringList() << "/select," << QDir::toNativeSeparators(first));
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(first).absolutePath()));
#endif
    Q_UNUSED(url);
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
    menu.addSeparator();
    QAction *actCopy = menu.addAction("复制到...(C)");
    QAction *actMove = menu.addAction("移动到...(M)");
    QAction *actReveal = menu.addAction("在资源管理器中显示(&E)");
    connect(actRename, &QAction::triggered, this, &ThumbnailPanel::renameSelected);
    connect(actTrash, &QAction::triggered, this, &ThumbnailPanel::moveToTrashSelected);
    connect(actCopy, &QAction::triggered, this, &ThumbnailPanel::copySelectedTo);
    connect(actMove, &QAction::triggered, this, &ThumbnailPanel::moveSelectedTo);
    connect(actReveal, &QAction::triggered, this, &ThumbnailPanel::revealSelected);
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
