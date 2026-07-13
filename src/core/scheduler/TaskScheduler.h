#pragma once

#include <QRunnable>
#include <QThreadPool>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>

// 统一优先级任务调度器：所有后台工作（元数据/解码/缩略图/分析/IO）都经它提交，
// 按 Priority 路由到 5 个独立线程池（队列），避免 ad-hoc QThread 失控，并让
// 低优先级的后台工作（预取/预加载）永不饿死 UI 路径。
//
// 注意：不继承 QObject（无信号/槽）；QThreadPool/QRunnable 为 QtCore 线程原语，
// 作为实现细节保留。对外不暴露任何 QWidget/UI 类型。
class TaskScheduler
{
public:
    // 旧的任务池分类（保留为兼容性别名，源码无需改动即可编译）。
    enum PoolType { MetadataPool, DecodePool, ThumbnailPool, AnalysisPool, IOPool };

    // 规范化的优先级分层（UI 最高，Background 最低）。队列索引即按优先级降序排列。
    enum class Priority : int { UI, Decode, Thumbnail, Analysis, Background };

    // 单任务的运行上下文：取消令牌 + 进度汇报。所有字段均为线程安全。
    struct TaskContext {
        std::shared_ptr<std::atomic<bool>> cancel =
            std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<std::atomic<int>> progress =
            std::make_shared<std::atomic<int>>(0);
        std::function<void(int)> onProgress;

        void requestCancel() { *cancel = true; }
        bool isCancelled() const { return cancel->load(std::memory_order_relaxed); }
        int currentProgress() const { return progress->load(std::memory_order_relaxed); }
        void reportProgress(int p) {
            const int v = p < 0 ? 0 : (p > 100 ? 100 : p);
            progress->store(v, std::memory_order_relaxed);
            if (onProgress) onProgress(v);
        }
    };
    using TaskHandle = std::shared_ptr<TaskContext>;

    static TaskScheduler &instance();

    // 提交一个 QRunnable 到指定池（旧路径，兼容）。
    void submit(PoolType pool, QRunnable *task);

    // 提交带优先级的工作：work 在池线程执行并可检查 TaskContext 的取消/进度；
    // done（可选）执行完后投回调用者事件循环；onProgress（可选）在进度变化时回调。
    TaskHandle submit(Priority prio,
                      std::function<void(const TaskContext &)> work,
                      std::function<void()> done = {},
                      std::function<void(int)> onProgress = {});

    // 兼容重载：void() 风格的工作，无法直接访问 TaskContext。
    TaskHandle submit(PoolType pool,
                      std::function<void()> work,
                      std::function<void()> done = {});

    // 旧 PoolType → 规范化 Priority 的映射（IO 视为最高 UI 优先级）。
    static Priority toPriority(PoolType pool);

    // 设置某优先级队列的最大线程数。
    void setQueueMaxThreads(Priority prio, int n);
    // 兼容别名。
    void setPoolMaxThreads(PoolType pool, int n);

    // 取消先前提交的任务（尽力而为：已启动的任务需在 work 内轮询 isCancelled）。
    static void cancel(TaskHandle &h) { if (h) h->requestCancel(); }

private:
    TaskScheduler();
    QThreadPool *pool(PoolType p)   { return &m_priorityQueues[static_cast<int>(toPriority(p))]; }
    QThreadPool *pool(Priority p)   { return &m_priorityQueues[static_cast<int>(p)]; }

    static constexpr int kNumQueues = 5;
    QThreadPool m_priorityQueues[kNumQueues];
};
