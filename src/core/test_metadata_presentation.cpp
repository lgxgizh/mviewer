// MetadataPresentationService snapshot cache is a bounded LRU.
#include "core/metadata/MetadataPresentationService.h"
#include "core/scheduler/TaskScheduler.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <chrono>
#include <cstdio>
#include <string>

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

namespace
{

using mviewer::core::MetadataPresentationService;

std::string writeTemp(QTemporaryDir &dir, const char *name)
{
    const QString path = dir.filePath(QString::fromUtf8(name));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(name);
    return path.toStdString();
}

bool requestOne(MetadataPresentationService &service, const std::string &path)
{
    const uint64_t id = service.request(path, "lru-consumer", [](const auto &) {});
    if (id == 0)
        return false;
    return TaskScheduler::instance().drain(TaskScheduler::MetadataPool, std::chrono::seconds(5));
}

void testLruEvictsOldestAndTouchKeepsHit()
{
    printf("\n[MetadataPresentationService LRU]\n");
    QTemporaryDir dir;
    CHECK(dir.isValid(), "temp dir");
    const std::string a = writeTemp(dir, "a.txt");
    const std::string b = writeTemp(dir, "b.txt");
    const std::string c = writeTemp(dir, "c.txt");
    const std::string d = writeTemp(dir, "d.txt");
    CHECK(!a.empty() && !b.empty() && !c.empty() && !d.empty(), "fixtures written");

    auto &service = MetadataPresentationService::instance();
    service.setCacheCapacity(0);
    CHECK(service.cacheSize() == 0, "capacity 0 drops every snapshot");
    CHECK(requestOne(service, a), "request while capacity is 0");
    CHECK(service.cacheSize() == 0, "capacity 0 does not retain a snapshot");

    service.setCacheCapacity(2);
    CHECK(service.cacheCapacity() == 2, "capacity applied");
    CHECK(requestOne(service, a) && requestOne(service, b), "fill two entries");
    CHECK(service.cacheSize() == 2, "cache holds the cap");
    CHECK(requestOne(service, c), "third insert evicts");
    CHECK(service.cacheSize() == 2, "cache stays at the cap");
    CHECK(!service.cached(a).has_value(), "oldest snapshot was evicted");
    CHECK(service.cached(c).has_value(), "newest snapshot is present");
    // cached(b) is a hit and becomes most-recently used.
    CHECK(service.cached(b).has_value(), "middle snapshot is still present");
    CHECK(requestOne(service, d), "insert after a cache hit");
    CHECK(!service.cached(c).has_value(), "least recently touched entry is evicted");
    CHECK(service.cached(b).has_value(), "cached() hit is kept");
    CHECK(service.cached(d).has_value(), "new snapshot is present");

    service.setCacheCapacity(0);
    service.setCacheCapacity(2);
    CHECK(requestOne(service, a) && requestOne(service, b), "refill A and B");
    CHECK(requestOne(service, a), "worker re-read touches A");
    CHECK(requestOne(service, c), "insert C after the worker hit");
    CHECK(!service.cached(b).has_value(), "worker hit keeps A and evicts B");
    CHECK(service.cached(a).has_value() && service.cached(c).has_value(),
          "A and C remain after the worker hit");

    service.setCacheCapacity(1);
    CHECK(service.cacheSize() == 1, "shrinking capacity evicts immediately");
    service.setCacheCapacity(MetadataPresentationService::kDefaultCacheEntries);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testLruEvictsOldestAndTouchKeepsHit();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
