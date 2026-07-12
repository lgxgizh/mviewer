#pragma once

#include <QListWidget>
#include <QStringList>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QPixmap>
#include <QHash>

class ThumbnailWorker;

// Right-side gallery: shows ALL images in the current directory as a
// scrollable list/grid of thumbnails. Emitting itemClicked(path) means the
// user single-clicked an image (=> show the bottom-left preview + stats).
// itemDoubleClicked(path) means open the full viewer.
class ThumbnailPanel : public QListWidget
{
    Q_OBJECT

public:
    static constexpr int kThumbSize = 140;
    static constexpr int kMaxImages = 1000;

    enum SortMode { SortName, SortDate, SortSize, SortResolution };

    explicit ThumbnailPanel(QWidget *parent = nullptr);
    ~ThumbnailPanel() override;

    void setDirectory(const QString &path);
    void setSortMode(SortMode mode);

signals:
    void itemClicked(const QString &path);
    void itemDoubleClicked(const QString &path);

private:
    void startWorker();
    void stopWorker();

    ThumbnailWorker *m_worker = nullptr;
    QThread m_thread;
    QString m_currentDir;
    SortMode m_sortMode = SortName;
    QHash<QString, QListWidgetItem *> m_itemById;
};

// Background worker: reads each image at thumbnail resolution (fast, no
// full-decode) and returns a ready QPixmap. Uses an on-disk cache so a
// previously visited folder loads instantly.
class ThumbnailWorker : public QObject
{
    Q_OBJECT

public:
    explicit ThumbnailWorker(QObject *parent = nullptr);

    struct Request
    {
        QString path;
        int thumbSize;
        QString id; // unique per load request (dir + index)
    };

public slots:
    void enqueue(const Request &req);
    void stop();
    void process();

signals:
    void thumbnailReady(const QString &path, const QPixmap &pm,
                        const QString &id);
    void finished();

private:
    QPixmap makeThumbnail(const QString &path);

    bool m_stop = false;
    QMutex m_mutex;
    QWaitCondition m_cond;
    QQueue<Request> m_queue;
};
