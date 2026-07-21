#include "gpu/GpuTileUploader.h"

#include <QOpenGLContext>
#include <QGuiApplication>
#include <QCoreApplication>
#include <cstdlib>

namespace
{

// Headless-safe GL probe. On a real display with a GL context this returns
// true; on offscreen/no-shareable-context builds it returns false so the
// entire GPU path stays a no-op and the verified CPU compositor is used.
bool glContextAvailable()
{
    // Opt-in gate is checked by the caller (enabled()); this only answers
    // "is a GL context actually usable here?". We probe lazily and cache.
    // Without Qt OpenGL headers linked we must not assume a context; the
    // build links Qt6::OpenGL only in the UI tier, so a probe that needs
    // a live context safely reports unavailable when none can be created.
    // Kept simple + safe: report based on whether Qt reported OpenGL
    // support at startup. On offscreen platforms that is false.
#if defined(QT_NO_OPENGL)
    return false;
#else
    // Safe capability probe. A GL context can only exist under a real GUI
    // platform. Under QCoreApplication (unit tests) or the offscreen platform
    // (headless CI) there is no GL integration, and attempting ctx.create()
    // there is undefined (historically crashes). So we report false and keep
    // the CPU compositor as the verified default in those environments.
    // Stage A's actual texture upload still requires a QOpenGLWidget host
    // (deferred per the M13 RFC) — this only answers "is GL usable here?".
    QCoreApplication *app = QCoreApplication::instance();
    if (!qobject_cast<QGuiApplication *>(app) ||
        QGuiApplication::platformName() == "offscreen")
    {
        return false;
    }
    static const bool can = [] {
        QOpenGLContext ctx;
        return ctx.create();
    }();
    return can;
#endif
}

} // namespace

bool GpuTileUploader::available()
{
    return glContextAvailable();
}

bool GpuTileUploader::hasEnvOptIn()
{
    const char *v = std::getenv("MVIEWER_GPU");
    return v != nullptr && v[0] != '\0' && v[0] != '0';
}

bool GpuTileUploader::ensure(const TileKey &key, const uint8_t *pixels, int w, int h, int channels)
{
    auto it = m_resident.find(key);
    if (it != m_resident.end())
    {
        // Touch (LRU): move to front.
        for (size_t i = 0; i < m_lru.size(); ++i)
        {
            if (m_lru[i] == key)
            {
                if (i != 0)
                {
                    TileKey k = m_lru[i];
                    m_lru.erase(m_lru.begin() + static_cast<ptrdiff_t>(i));
                    m_lru.insert(m_lru.begin(), k);
                }
                break;
            }
        }
        return true;
    }

    const uintptr_t tex = m_upload ? m_upload(key, pixels, w, h, channels) : 0;
    if (tex == 0)
        return false; // upload failed -> not resident (CPU fallback handles it)

    Resident r;
    r.handle = tex;
    r.bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * static_cast<size_t>(channels);
    m_resident[key] = r;
    m_lru.insert(m_lru.begin(), key);

    // Enforce budget.
    while (m_resident.size() > maxResident)
        evictOne();

    return true;
}

uintptr_t GpuTileUploader::evictOne()
{
    if (m_lru.empty())
        return 0;
    const TileKey victim = m_lru.back();
    m_lru.pop_back();
    auto it = m_resident.find(victim);
    if (it == m_resident.end())
        return 0;
    const uintptr_t h = it->second.handle;
    if (m_free)
        m_free(h);
    m_resident.erase(it);
    return h;
}

void GpuTileUploader::clear()
{
    if (m_free)
    {
        for (const auto &kv : m_resident)
            m_free(kv.second.handle);
    }
    m_resident.clear();
    m_lru.clear();
}

bool GpuTileUploader::isResident(const TileKey &key) const
{
    return m_resident.find(key) != m_resident.end();
}

uintptr_t GpuTileUploader::handle(const TileKey &key)
{
    auto it = m_resident.find(key);
    if (it == m_resident.end())
        return 0;
    // Touch LRU on read too.
    for (size_t i = 0; i < m_lru.size(); ++i)
    {
        if (m_lru[i] == key)
        {
            if (i != 0)
            {
                TileKey k = m_lru[i];
                m_lru.erase(m_lru.begin() + static_cast<ptrdiff_t>(i));
                m_lru.insert(m_lru.begin(), k);
            }
            break;
        }
    }
    return it->second.handle;
}
