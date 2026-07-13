#include "core/scheduler/TaskScheduler.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

#include <memory>

namespace
{
// 包装 lambda 的 QRunnable：work 在池线程跑，done 投回 GUI 线程。
// TaskContext 随任务携带，work 可轮询取消令牌、上报进度。
class LambdaTask : public QRunnable
{
public:
    LambdaTask(TaskScheduler::TaskHandle ctx,
               std::function<void(const TaskScheduler::TaskContext &)> work,
               std::function<void()> done)
        : m_ctx(std::move(ctx)), m_work(std::move(work)), m_done(std::move(done))
    {
        setAutoDelete(true);
    }
    void run() override
    {
        if (m_work && !m_ctx->isCancelled())
            m_work(*m_ctx);
        if (m_done) {
            QObject *tgt = QCoreApplication::instance();
            if (tgt) {
                QMetaObject::invokeMethod(
                    tgt, [d = std::move(m_done)]() { d(); },
                    Qt::QueuedConnection);
            } else {
                m_done();
            }
        }
    }

private:
    TaskScheduler::TaskHandle m_ctx;
    std::function<void(const TaskScheduler::TaskContext &)> m_work;
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
    // 各优先级队列的线程预算：UI/解码给足；缩略图最忙；分析/后台限制以免卡 UI。
    setQueueMaxThreads(Priority::UI,        std::max(1, n));
    setQueueMaxThreads(Priority::Decode,    std::max(1, n));
    setQueueMaxThreads(Priority::Thumbnail, std::max(2, n));
    setQueueMaxThreads(Priority::Analysis,  std::max(1, n / 2));
    setQueueMaxThreads(Priority::Background, std::max(1, n / 2));
}

TaskScheduler::Priority TaskScheduler::toPriority(PoolType pool)
{
    switch (pool) {
    case IOPool:       return Priority::UI;
    case DecodePool:   return Priority::Decode;
    case ThumbnailPool: return Priority::Thumbnail;
    case AnalysisPool: return Priority::Analysis;
    case MetadataPool: return Priority::Background;
    }
    return Priority::Background;
}

void TaskScheduler::submit(PoolType p, QRunnable *task)
{
    pool(p)->start(task);
}

TaskScheduler::TaskHandle TaskScheduler::submit(
    Priority prio,
    std::function<void(const TaskContext &)> work,
    std::function<void()> done,
    std::function<void(int)> onProgress)
{
    auto ctx = std::make_shared<TaskContext>();
    ctx->onProgress = std::move(onProgress);
    pool(prio)->start(new LambdaTask(ctx, std::move(work), std::move(done)));
    return ctx;
}

TaskScheduler::TaskHandle TaskScheduler::submit(PoolType pool,
                                                std::function<void()> work,
                                                std::function<void()> done)
{
    return submit(toPriority(pool),
                  [w = std::move(work)](const TaskContext &) { if (w) w(); },
                  std::move(done));
}

void TaskScheduler::setQueueMaxThreads(Priority prio, int n)
{
    pool(prio)->setMaxThreadCount(std::max(1, n));
}

void TaskScheduler::setPoolMaxThreads(PoolType pool, int n)
{
    setQueueMaxThreads(toPriority(pool), n);
}
