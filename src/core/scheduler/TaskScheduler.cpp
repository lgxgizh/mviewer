#include "core/scheduler/TaskScheduler.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

namespace
{
// 包装 lambda 的 QRunnable：work 在池线程跑，done 投回 GUI 线程。
// 若 cancel 令牌被置位，则跳过 work 且不调用 done（RFC-004）。
class LambdaTask : public QRunnable
{
public:
    LambdaTask(std::function<void()> work, std::function<void()> done,
               TaskScheduler::CancelToken cancel, TaskScheduler::Progress progress)
        : m_work(std::move(work)), m_done(std::move(done)),
          m_cancel(std::move(cancel)), m_progress(std::move(progress))
    {
        setAutoDelete(true);
    }
    void run() override
    {
        if (m_cancel && m_cancel->load())
            return; // 已取消：不执行、不回调
        if (m_work)
            m_work();
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
    std::function<void()> m_work;
    std::function<void()> m_done;
    TaskScheduler::CancelToken m_cancel;
    TaskScheduler::Progress m_progress;
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
    // UI 队列：用户操作必须立即得到线程，给足线程（至少 2）。
    m_priorityQueues[static_cast<int>(Priority::UI)].setMaxThreadCount(std::max(2, n));
    // 解码：当前图像，给足线程。
    m_priorityQueues[static_cast<int>(Priority::Decode)].setMaxThreadCount(std::max(2, n));
    // 缩略图：最忙，给足线程。
    m_priorityQueues[static_cast<int>(Priority::Thumbnail)].setMaxThreadCount(std::max(2, n));
    // 分析：可能吃满 CPU，限制以免卡 UI。
    m_priorityQueues[static_cast<int>(Priority::Analysis)].setMaxThreadCount(std::max(1, n / 2));
    // 后台：最低优先级，限制并发避免抢占前台资源。
    m_priorityQueues[static_cast<int>(Priority::Background)].setMaxThreadCount(std::max(1, n / 2));
}

QThreadPool *TaskScheduler::pool(Priority p)
{
    return &m_priorityQueues[static_cast<int>(p)];
}

void TaskScheduler::submit(Priority p, QRunnable *task)
{
    pool(p)->start(task);
}

void TaskScheduler::submit(Priority p, std::function<void()> work,
                           std::function<void()> done, CancelToken cancel,
                           Progress progress)
{
    pool(p)->start(new LambdaTask(std::move(work), std::move(done),
                                  std::move(cancel), std::move(progress)));
}

void TaskScheduler::setPoolMaxThreads(Priority p, int n)
{
    pool(p)->setMaxThreadCount(std::max(1, n));
}
