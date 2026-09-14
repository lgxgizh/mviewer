#include "display/DisplayColorContextProvider.h"

#include <QFile>
#include <QSettings>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>
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

std::mutex &profileMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<uintptr_t, WindowProfileState> &profileStates()
{
    static std::unordered_map<uintptr_t, WindowProfileState> states;
    return states;
}

mviewer::core::DisplayColorContext withGeneration(uintptr_t windowId,
                                                  mviewer::core::DisplayColorContext context)
{
    std::lock_guard<std::mutex> lock(profileMutex());
    auto &state = profileStates()[windowId];
    if (state.fingerprint != context.fingerprint)
    {
        state.fingerprint = context.fingerprint;
        ++state.generation;
    }
    context.generation = state.generation;
    return context;
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
    g_cmsEnabled.store(enabled, std::memory_order_relaxed);
    g_cmsLoaded.store(true, std::memory_order_relaxed);
    QSettings settings;
    settings.setValue("display/colorManagement", enabled);
}

mviewer::core::DisplayColorContext DisplayColorContextProvider::forWindow(const QWindow *window)
{
    const uintptr_t windowId = window ? static_cast<uintptr_t>(window->winId()) : 0;
    if (!isColorManagementEnabled())
    {
        return withGeneration(windowId, mviewer::core::DisplayColorContext::raw());
    }
#if defined(Q_OS_WIN)
    if (window && windowId != 0)
    {
        HDC dc = GetDC(reinterpret_cast<HWND>(windowId));
        if (dc)
        {
            DWORD chars = 0;
            GetICMProfileW(dc, &chars, nullptr);
            if (chars > 1)
            {
                std::wstring profilePath(chars, L'\0');
                if (GetICMProfileW(dc, &chars, profilePath.data()))
                {
                    profilePath.resize(chars);
                    const QString path = QString::fromWCharArray(profilePath.c_str());
                    QFile file(path);
                    if (file.open(QIODevice::ReadOnly))
                    {
                        // ICC profiles are small; a bogus/spoofed system profile
                        // path must not turn into an unbounded allocation, and
                        // the read is bounded by the real size so a small
                        // profile does not allocate the whole cap.
                        constexpr qint64 kMaxIccProfileBytes = 32LL * 1024 * 1024;
                        const QByteArray bytes =
                            file.read(std::min<qint64>(file.size(), kMaxIccProfileBytes));
                        ReleaseDC(reinterpret_cast<HWND>(windowId), dc);
                        auto profile = std::vector<uint8_t>(
                            reinterpret_cast<const uint8_t *>(bytes.constData()),
                            reinterpret_cast<const uint8_t *>(bytes.constData()) + bytes.size());
                        return withGeneration(
                            windowId,
                            mviewer::core::DisplayColorContext::fromIccProfile(std::move(profile)));
                    }
                }
            }
            ReleaseDC(reinterpret_cast<HWND>(windowId), dc);
        }
    }
#endif
    return withGeneration(windowId, mviewer::core::DisplayColorContext::sRGB());
}
