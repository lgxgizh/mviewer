#pragma once

#include <QRunnable>
#include <QThreadPool>
#include <functional>
#include <memory>
#include <atomic>

// 统一任务调度器（RFC-004）：5 个优先级队列，所有后台工作
// （IO / 解码 / 缩略图 / 分析 / 预取）按优先级提交，高优先级
// 任务拥有独立线程池，永远不会被后台工作阻塞。
// 注意：不继承 QObject（无信号/槽）；QThreadPool/QRunnable 为 QtCore
// 线程原语，作为实现细节保留。接口不暴露 UI 类型。
class TaskScheduler
{
public:
    // 优先级（数值越小优先级越高）。同时是缓存所有者对各队列的语义分层。
    enum class Priority : int {
        UI = 0,        // 最高：导航、缩放、用户主动触发
        Decode,        // 当前图像解码
        Thumbnail,     // 可见缩略图
        Analysis,      // 直方图 / 差异 / PSNR
        Background,    // 最低：预加载、预取
        Count
    };

    // 向后兼容别名（旧代码仍可用 DecodePool 等名字）。
    using PoolType = Priority;
    static constexpr Priority MetadataPool = Priority::UI;
    static constexpr Priority DecodePool    = Priority::Decode;
    static constexpr Priority ThumbnailPool = Priority::Thumbnail;
    static constexpr Priority AnalysisPool  = Priority::Analysis;
    static constexpr Priority IOPool        = Priority::Background;

    // 取消令牌 / 进度（任务内部周期性检查 cancel，并写入 progress）。
    using CancelToken = std::shared_ptr<std::atomic<bool>>;
    using Progress    = std::shared_ptr<std::atomic<int>>;

    static TaskScheduler &instance();

    // 提交一个 QRunnable 到指定优先级队列。
    void submit(Priority p, QRunnable *task);

    // 便捷封装：work 在池线程执行；done（可选）执行完后投回调用者事件循环。
    // cancel 非空的，若提交后变为 true，则跳过 work 且不调用 done。
    // progress 供调用方观测（0-100）；work 内自行更新。
    void submit(Priority p, std::function<void()> work,
                std::function<void()> done = {},
                CancelToken cancel = nullptr, Progress progress = nullptr);

    // 设置某优先级队列最大线程数（默认按优先级与 idealThreadCount 调优）。
    void setPoolMaxThreads(Priority p, int n);

    // 工厂：创建新的取消令牌。
    static CancelToken makeCancelToken() { return std::make_shared<std::atomic<bool>>(false); }

    // 取消某令牌（所有引用同一令牌的任务将停止）。
    static void cancel(CancelToken &tok) { if (tok) tok->store(true); }

private:
    TaskScheduler();
    QThreadPool *pool(Priority p);

    QThreadPool m_priorityQueues[static_cast<int>(Priority::Count)];
};
