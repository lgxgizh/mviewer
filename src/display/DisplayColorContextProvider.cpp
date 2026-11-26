#include "display/DisplayColorContextProvider.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QWindow>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if defined(Q_OS_WIN)
#define NOMINMAX
#include <windows.h>
#endif

namespace
{

std::atomic<bool> g_cmsLoaded{false};
std::atomic<bool> g_cmsEnabled{true};

struct WindowProfileState
{
    std::string fingerprint;
    uint64_t generation = 0;
};

struct ProviderState
{
    std::mutex mutex;
    DisplayColorContextProvider::ProfileLoader loader;
    std::string screenOverride;
    int loadCount = 0;
    uint64_t epoch = 0;
    std::unordered_set<const QWindow *> trackedWindows;
    std::unordered_map<std::string, mviewer::core::DisplayColorContext> profiles;
    std::unordered_map<uintptr_t, WindowProfileState> windows;
};

ProviderState &providerState()
{
    static ProviderState state;
    return state;
}

mviewer::core::DisplayColorContext applyGenerationLocked(uintptr_t windowId,
                                                         mviewer::core::DisplayColorContext context)
{
    auto &state = providerState().windows[windowId];
    if (state.fingerprint != context.fingerprint)
    {
        state.fingerprint = context.fingerprint;
        ++state.generation;
    }
    context.generation = state.generation;
    return context;
}

mviewer::core::DisplayColorContext applyGeneration(uintptr_t windowId,
                                                   mviewer::core::DisplayColorContext context)
{
    std::lock_guard<std::mutex> lock(providerState().mutex);
    return applyGenerationLocked(windowId, std::move(context));
}

std::string screenIdentityKey(const QWindow *window)
{
    {
        std::lock_guard<std::mutex> lock(providerState().mutex);
        if (!providerState().screenOverride.empty())
            return providerState().screenOverride;
    }
    const QScreen *screen = window ? window->screen() : nullptr;
    if (!screen)
    {
        if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
            screen = app->primaryScreen();
    }
    if (!screen)
        return std::string("none");
    QString key = screen->name();
    key += QLatin1Char('\n');
    key += screen->manufacturer();
    key += QLatin1Char('\n');
    key += screen->model();
    key += QLatin1Char('\n');
    key += screen->serialNumber();
    return key.toStdString();
}

#if defined(Q_OS_WIN)
std::vector<uint8_t> loadWindowsIccProfile(const QWindow *window)
{
    const uintptr_t windowId = window ? static_cast<uintptr_t>(window->winId()) : 0;
    if (!window || windowId == 0)
        return {};
    HDC dc = GetDC(reinterpret_cast<HWND>(windowId));
    if (!dc)
        return {};
    DWORD chars = 0;
    GetICMProfileW(dc, &chars, nullptr);
    if (chars <= 1)
    {
        ReleaseDC(reinterpret_cast<HWND>(windowId), dc);
        return {};
    }
    std::wstring profilePath(chars, L'\0');
    if (!GetICMProfileW(dc, &chars, profilePath.data()))
    {
        ReleaseDC(reinterpret_cast<HWND>(windowId), dc);
        return {};
    }
    profilePath.resize(chars);
    const QString path = QString::fromWCharArray(profilePath.c_str());
    ReleaseDC(reinterpret_cast<HWND>(windowId), dc);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    // ICC profiles are small; a bogus/spoofed system profile path must not
    // turn into an unbounded allocation, and the read is bounded by the real
    // size so a small profile does not allocate the whole cap.
    constexpr qint64 kMaxIccProfileBytes = 32LL * 1024 * 1024;
    const QByteArray bytes = file.read(std::min<qint64>(file.size(), kMaxIccProfileBytes));
    return {reinterpret_cast<const uint8_t *>(bytes.constData()),
            reinterpret_cast<const uint8_t *>(bytes.constData()) + bytes.size()};
}
#endif

#if defined(Q_OS_LINUX)
// Linux has no per-window ICM API: the profile lives with colord (or in the
// usual color directories). We look in the well-known locations and stop at the
// first readable profile. $MVIEWER_ICC is an explicit override for a machine
// whose profile lives somewhere else (or for a test).
std::vector<uint8_t> loadLinuxIccProfile(const QWindow *window)
{
    Q_UNUSED(window);
    const auto readBounded = [](const QString &path) -> std::vector<uint8_t>
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        // ICC profiles are small; a bogus path must not become an unbounded
        // allocation. Bound by the real size so a small profile stays small.
        constexpr qint64 kMaxIccProfileBytes = 32LL * 1024 * 1024;
        const QByteArray bytes = file.read(std::min<qint64>(file.size(), kMaxIccProfileBytes));
        return {reinterpret_cast<const uint8_t *>(bytes.constData()),
                reinterpret_cast<const uint8_t *>(bytes.constData()) + bytes.size()};
    };

    const QByteArray overridePath = qgetenv("MVIEWER_ICC");
    if (!overridePath.isEmpty())
    {
        if (auto bytes = readBounded(QString::fromLocal8Bit(overridePath)); !bytes.empty())
            return bytes;
    }

    const QByteArray xdgConfig = qgetenv("XDG_CONFIG_HOME");
    const QByteArray home = qgetenv("HOME");
    QStringList dirs;
    if (!xdgConfig.isEmpty())
        dirs << QString::fromLocal8Bit(xdgConfig) + QStringLiteral("/color/icc");
    if (!home.isEmpty())
        dirs << QString::fromLocal8Bit(home) + QStringLiteral("/.config/color/icc");
    dirs << QStringLiteral("/usr/share/color/icc") << QStringLiteral("/var/lib/colord/icc");

    // Pick within a directory: a file whose stem names this display wins, a
    // single candidate needs no guessing, and otherwise the name-sorted first
    // stays but is reported - there is no per-window ICM on Linux, so the user
    // must be able to see which profile was taken.
    const QString identity =
        window ? (window->screen()->name() + QLatin1Char(' ') + window->screen()->manufacturer() +
                  QLatin1Char(' ') + window->screen()->model())
               : QString();
    auto score = [&identity](const QString &stem)
    {
        if (identity.isEmpty())
            return 0;
        int hits = 0;
        for (const QString &token : identity.split(QLatin1Char(' '), Qt::SkipEmptyParts))
        {
            if (token.size() < 3)
                continue;
            if (stem.contains(token, Qt::CaseInsensitive))
                ++hits;
        }
        return hits;
    };

    for (const QString &dir : dirs)
    {
        QDir candidate(dir);
        if (!candidate.exists())
            continue;
        const QFileInfoList entries =
            candidate.entryInfoList(QStringList{QStringLiteral("*.icc"), QStringLiteral("*.icm")},
                                    QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        if (entries.isEmpty())
            continue;
        const QFileInfo *chosen = &entries.first();
        if (entries.size() > 1)
        {
            int best = -1;
            for (const QFileInfo &info : entries)
            {
                const int s = score(info.completeBaseName());
                if (s > best)
                {
                    best = s;
                    chosen = &info;
                }
            }
            if (best <= 0)
                qInfo().noquote() << "[DisplayColor] Several ICC profiles in" << dir
                                  << "- using" << chosen->absoluteFilePath()
                                  << "(set MVIEWER_ICC to choose explicitly)";
        }
        if (auto bytes = readBounded(chosen->absoluteFilePath()); !bytes.empty())
            return bytes;
    }
    return {};
}
#endif

std::vector<uint8_t> loadPlatformMonitorProfile(const QWindow *window)
{
#if defined(Q_OS_WIN)
    return loadWindowsIccProfile(window);
#elif defined(Q_OS_LINUX)
    return loadLinuxIccProfile(window);
#else
    // macOS ColorSync is still the sRGB fallback.
    Q_UNUSED(window);
    return {};
#endif
}

std::optional<mviewer::core::DisplayColorContext> cachedProfile(const std::string &screenKey)
{
    std::lock_guard<std::mutex> lock(providerState().mutex);
    const auto cached = providerState().profiles.find(screenKey);
    if (cached == providerState().profiles.end())
        return std::nullopt;
    return cached->second;
}

std::vector<uint8_t> loadProfileBytes(const QWindow *window)
{
    DisplayColorContextProvider::ProfileLoader loader;
    {
        std::lock_guard<std::mutex> lock(providerState().mutex);
        auto &state = providerState();
        ++state.loadCount;
        loader = state.loader;
    }
    if (loader)
        return loader(window);
    return loadPlatformMonitorProfile(window);
}

mviewer::core::DisplayColorContext contextFromProfileBytes(std::vector<uint8_t> bytes)
{
    if (bytes.empty())
        return mviewer::core::DisplayColorContext::sRGB();
    return mviewer::core::DisplayColorContext::fromIccProfile(std::move(bytes));
}

void trackWindow(const QWindow *window)
{
    if (!window)
        return;
    {
        std::lock_guard<std::mutex> lock(providerState().mutex);
        if (!providerState().trackedWindows.insert(window).second)
            return;
    }
    const auto id = static_cast<uintptr_t>(window->winId());
    QObject::connect(window, &QObject::destroyed, DisplayColorContextProvider::colorSpaceHub(),
                     [window, id]
                     {
                         std::lock_guard<std::mutex> lock(providerState().mutex);
                         providerState().windows.erase(id);
                         providerState().trackedWindows.erase(window);
                     });
}

mviewer::core::DisplayColorContext resolveManagedContext(const QWindow *window, uintptr_t windowId)
{
    const std::string screenKey = screenIdentityKey(window);
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        if (auto cached = cachedProfile(screenKey))
            return applyGeneration(windowId, std::move(*cached));
        uint64_t epoch = 0;
        {
            std::lock_guard<std::mutex> lock(providerState().mutex);
            epoch = providerState().epoch;
        }
        auto context = contextFromProfileBytes(loadProfileBytes(window));
        std::lock_guard<std::mutex> lock(providerState().mutex);
        if (epoch == providerState().epoch)
        {
            const auto inserted = providerState().profiles.emplace(screenKey, std::move(context));
            return applyGenerationLocked(windowId, inserted.first->second);
        }
    }
    // Never republish a profile invalidated while the loader was outside the
    // lock. A later refresh retries; the native watcher queues/coalesces it.
    return applyGeneration(windowId, mviewer::core::DisplayColorContext::sRGB());
}

void clearCachedProfilesLocked()
{
    ++providerState().epoch;
    providerState().profiles.clear();
}

} // namespace

bool DisplayColorContextProvider::isColorManagementEnabled()
{
    if (!g_cmsLoaded.load(std::memory_order_relaxed))
    {
        QSettings settings;
        g_cmsEnabled.store(settings.value("display/colorManagement", true).toBool(),
                           std::memory_order_relaxed);
        g_cmsLoaded.store(true, std::memory_order_relaxed);
    }
    return g_cmsEnabled.load(std::memory_order_relaxed);
}

void DisplayColorContextProvider::setColorManagementEnabled(bool enabled)
{
    isColorManagementEnabled();
    const bool previous = g_cmsEnabled.exchange(enabled, std::memory_order_relaxed);
    g_cmsLoaded.store(true, std::memory_order_relaxed);
    QSettings settings;
    settings.setValue("display/colorManagement", enabled);
    if (previous != enabled)
        notifySystemColorSpaceChanged();
}

mviewer::core::DisplayColorContext DisplayColorContextProvider::forWindow(const QWindow *window)
{
    colorSpaceHub();
    trackWindow(window);
    const uintptr_t windowId = window ? static_cast<uintptr_t>(window->winId()) : 0;
    if (!isColorManagementEnabled())
        return applyGeneration(windowId, mviewer::core::DisplayColorContext::raw());
    if (!window)
    {
        // A null HWND cannot query the monitor. Do not poison the real screen
        // cache with this fallback; injected test loaders can model a screen.
        std::lock_guard<std::mutex> lock(providerState().mutex);
        if (!providerState().loader)
            return applyGenerationLocked(windowId, mviewer::core::DisplayColorContext::sRGB());
    }
    return resolveManagedContext(window, windowId);
}

bool DisplayColorContextProvider::targetUnchanged(const QWindow *window,
                                                  const mviewer::core::DisplayColorContext &current)
{
    if (!isColorManagementEnabled())
        return !current.colorManagementEnabled;
    if (!window)
        return false;
    const std::string screenKey = screenIdentityKey(window);
    std::lock_guard<std::mutex> lock(providerState().mutex);
    const auto it = providerState().profiles.find(screenKey);
    return it != providerState().profiles.end() && current.colorManagementEnabled &&
           it->second.fingerprint == current.fingerprint;
}

int DisplayColorContextProvider::windowStateCountForTest()
{
    std::lock_guard<std::mutex> lock(providerState().mutex);
    return static_cast<int>(providerState().windows.size());
}

void DisplayColorContextProvider::notifySystemColorSpaceChanged()
{
    {
        std::lock_guard<std::mutex> lock(providerState().mutex);
        clearCachedProfilesLocked();
    }
    if (DisplayColorSpaceHub *hub = colorSpaceHub())
        emit hub->colorSpaceChanged();
}

void DisplayColorContextProvider::setProfileLoaderForTest(ProfileLoader loader)
{
    std::lock_guard<std::mutex> lock(providerState().mutex);
    providerState().loader = std::move(loader);
}

int DisplayColorContextProvider::profileLoadCountForTest()
{
    std::lock_guard<std::mutex> lock(providerState().mutex);
    return providerState().loadCount;
}

void DisplayColorContextProvider::setScreenIdentityForTest(std::string identity)
{
    std::lock_guard<std::mutex> lock(providerState().mutex);
    providerState().screenOverride = std::move(identity);
}

void DisplayColorContextProvider::resetForTest()
{
    std::lock_guard<std::mutex> lock(providerState().mutex);
    auto &state = providerState();
    state.loader = {};
    state.screenOverride.clear();
    state.loadCount = 0;
    ++state.epoch;
    state.profiles.clear();
    state.windows.clear();
    g_cmsEnabled.store(true, std::memory_order_relaxed);
    g_cmsLoaded.store(true, std::memory_order_relaxed);
}
