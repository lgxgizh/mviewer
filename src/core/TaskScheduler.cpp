#include "TaskScheduler.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

namespace
{
// 包装 lambda 的 QRunnable：work 在池线程跑，done 投回 GUI 线程。
class LambdaTask : public QRunnable
{
public:
    LambdaTask(std::function<void()> work, std::function<void()> done)
        : m_work(std::move(work)), m_done(std::move(done))
    {
        setAutoDelete(true);
    }
    void run() override
    {
        if (m_work)
            m_work();
        if (m_done) {
            QObject *tgt = QCoreApplication::instance();
            if (tgt) {
                QMetaObject::invokeMethod(
                    tgt, [d = std::move(m_done)]() { d(); },
                    Qt::QueuedConnection);
            } else if (m_done) {
                m_done();
            }
        }
    }

private:
    std::function<void()> m_work;
    std::function<void()> m_done;
};
} // namespace

TaskScheduler &TaskScheduler::instance()
{
    static TaskScheduler inst;
    return inst;
}

TaskScheduler::TaskScheduler()
{
    const int n = QThread::idealThreadCount();
    for (int i = 0; i < 4; ++i)
        m_pools[i].setMaxThreadCount(std::max(1, n));
    // 缩略图池通常最忙，给足线程；分析池可能吃满 CPU，限制一下避免卡 UI。
    m_pools[ThumbnailPool].setMaxThreadCount(std::max(2, n));
    m_pools[AnalysisPool].setMaxThreadCount(std::max(1, n / 2));
}

QThreadPool *TaskScheduler::pool(PoolType p)
{
    return &m_pools[p];
}

void TaskScheduler::submit(PoolType p, QRunnable *task)
{
    pool(p)->start(task);
}

void TaskScheduler::submit(PoolType p, std::function<void()> work,
                           std::function<void()> done)
{
    pool(p)->start(new LambdaTask(std::move(work), std::move(done)));
}

void TaskScheduler::setPoolMaxThreads(PoolType p, int n)
{
    pool(p)->setMaxThreadCount(std::max(1, n));
}
