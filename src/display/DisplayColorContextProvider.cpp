#include "display/DisplayColorContextProvider.h"

#include <QFile>

#include <mutex>
#include <unordered_map>
#include <utility>

#if defined(Q_OS_WIN)
#define NOMINMAX
#include <windows.h>
#endif

namespace
{

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

mviewer::core::DisplayColorContext withGeneration(
    uintptr_t windowId, mviewer::core::DisplayColorContext context)
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

mviewer::core::DisplayColorContext DisplayColorContextProvider::forWindow(const QWindow *window)
{
    const uintptr_t windowId = window ? static_cast<uintptr_t>(window->winId()) : 0;
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
                        const QByteArray bytes = file.readAll();
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
