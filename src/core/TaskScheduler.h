#pragma once

#include <QObject>
#include <QRunnable>
#include <QThreadPool>
#include <QString>
#include <functional>

// 统一任务调度器：所有后台工作（解码/缩略图/分析/IO）都经它提交，
// 按 PoolType 路由到独立线程池，避免 ad-hoc QThread 失控。
class TaskScheduler : public QObject
{
    Q_OBJECT

public:
    enum PoolType { DecodePool, ThumbnailPool, AnalysisPool, IOPool };

    static TaskScheduler &instance();

    // 提交一个 QRunnable 到指定池。
    void submit(PoolType pool, QRunnable *task);

    // 便捷封装：work 在池线程执行；done（可选）执行完后投回调用者事件循环。
    void submit(PoolType pool, std::function<void()> work,
                std::function<void()> done = {});

    // 设置某池最大线程数（默认 idealThreadCount）。
    void setPoolMaxThreads(PoolType pool, int n);

private:
    TaskScheduler();
    QThreadPool *pool(PoolType p);

    QThreadPool m_pools[4];
};
