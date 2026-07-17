#include <QCoreApplication>
// M6 unit tests: TaskScheduler dependency + priority mapping.
#include "core/scheduler/TaskScheduler.h"

#include <cstdio>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            g_pass++;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

static void testTaskSchedulerDependency()
{
    printf("\n[TaskScheduler Dependency]\n");
    auto &sched = TaskScheduler::instance();

    auto h1 = sched.submit(TaskScheduler::Priority::Background,
                           [](const TaskScheduler::TaskContext &) {});
    auto h2 = sched.submit(TaskScheduler::Priority::Background,
                           [](const TaskScheduler::TaskContext &) {});
    CHECK(h1->id != h2->id, "TaskId auto-increments");
    CHECK(h1->id > 0, "TaskId starts from 1");

    TaskScheduler::TaskId depId = 999999;
    auto h3 = sched.submit(TaskScheduler::Priority::Background,
                           [](const TaskScheduler::TaskContext &) {}, {depId});
    CHECK(h3->id > 0, "Dep task accepted");
    CHECK(h3->dependencies.size() == 1, "Dep recorded");

    auto found = sched.handle(h3->id);
    CHECK(found != nullptr, "handle() finds task");

    TaskScheduler::cancelTree(depId);

    auto h4 = sched.submit(TaskScheduler::DecodePool, []() {}, []() {});
    CHECK(h4->id > 0, "Compat submit works");

    CHECK(TaskScheduler::toPriority(TaskScheduler::DecodePool) == TaskScheduler::Priority::Decode,
          "toPriority(DecodePool) = Decode");
    CHECK(TaskScheduler::toPriority(TaskScheduler::MetadataPool) ==
              TaskScheduler::Priority::Background,
          "toPriority(MetadataPool) = Background");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("=== Scheduler Tests (M6) ===\n");
    fflush(stdout);

    testTaskSchedulerDependency();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
