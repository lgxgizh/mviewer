#include "gpu/GpuTileUploader.h"

#include <QByteArray>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <cstdlib>
#include <cstring>

// ─── capability probes ───────────────────────────────────────────────────────

bool GpuTileUploader::available()
{
    // A current QOpenGLContext means we can issue GL calls (real Stage A host
    // or a test that made a context current). Headless QCoreApplication /
    // offscreen without GL returns false — CPU path stays the default.
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    return ctx != nullptr && ctx->isValid();
}

bool GpuTileUploader::enabled()
{
    if (!available())
        return false;
    const char *env = std::getenv("MVIEWER_GPU");
    if (!env || env[0] == '\0')
        return false;
    // Accept common truthy spellings.
    if (std::strcmp(env, "1") == 0 || std::strcmp(env, "true") == 0 ||
        std::strcmp(env, "TRUE") == 0 || std::strcmp(env, "yes") == 0 ||
        std::strcmp(env, "YES") == 0 || std::strcmp(env, "on") == 0 || std::strcmp(env, "ON") == 0)
        return true;
    return false;
}

// ─── residency ───────────────────────────────────────────────────────────────

bool GpuTileUploader::ensure(const TileKey &key, const uint8_t *pixels, int w, int h, int channels)
{
    // Injected callbacks (unit tests) always run; real GL path only when
    // enabled() so the CPU compositor remains the verified default.
    const bool useInjected = static_cast<bool>(m_upload);
    if (!useInjected && !enabled())
        return false;

    auto it = m_map.find(key);
    if (it != m_map.end())
    {
        touch(key);
        return it->second.handle != 0;
    }

    if (w <= 0 || h <= 0 || channels <= 0)
        return false;
    // Real GL upload needs pixel data; injected tests may pass nullptr.
    if (!useInjected && !pixels)
        return false;

    const uintptr_t hnd = doUpload(key, pixels, w, h, channels);
    if (hnd == 0)
        return false;

    m_lru.push_back(key);
    Entry e;
    e.handle = hnd;
    e.lruIt = std::prev(m_lru.end());
    m_map.emplace(key, e);
    evictIfNeeded();
    return true;
}

bool GpuTileUploader::isResident(const TileKey &key) const
{
    return m_map.find(key) != m_map.end();
}

uintptr_t GpuTileUploader::handle(const TileKey &key) const
{
    auto it = m_map.find(key);
    return it == m_map.end() ? 0 : it->second.handle;
}

void GpuTileUploader::clear()
{
    for (auto &kv : m_map)
        doFree(kv.second.handle);
    m_map.clear();
    m_lru.clear();
}

void GpuTileUploader::touch(const TileKey &key)
{
    auto it = m_map.find(key);
    if (it == m_map.end())
        return;
    m_lru.erase(it->second.lruIt);
    m_lru.push_back(key);
    it->second.lruIt = std::prev(m_lru.end());
}

void GpuTileUploader::evictIfNeeded()
{
    while (static_cast<int>(m_map.size()) > maxResident && !m_lru.empty())
    {
        const TileKey oldest = m_lru.front();
        m_lru.pop_front();
        auto it = m_map.find(oldest);
        if (it == m_map.end())
            continue;
        doFree(it->second.handle);
        m_map.erase(it);
    }
}

uintptr_t GpuTileUploader::doUpload(const TileKey &key, const uint8_t *pixels, int w, int h,
                                    int channels)
{
    if (m_upload)
        return m_upload(key, pixels, w, h, channels);

    // Real GL path — requires a current context (Stage A host).
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx || !ctx->isValid() || !pixels)
        return 0;
    QOpenGLFunctions *gl = ctx->functions();
    if (!gl)
        return 0;

    GLenum format = GL_RGBA;
    GLenum internal = GL_RGBA;
    if (channels == 1)
    {
        format = GL_RED;
        internal = GL_R8;
    }
    else if (channels == 3)
    {
        format = GL_RGB;
        internal = GL_RGB;
    }
    else if (channels == 4)
    {
        format = GL_RGBA;
        internal = GL_RGBA;
    }
    else
    {
        return 0;
    }

    GLuint tex = 0;
    gl->glGenTextures(1, &tex);
    if (tex == 0)
        return 0;
    gl->glBindTexture(GL_TEXTURE_2D, tex);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Tight packing for odd widths (common on edge tiles).
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internal), w, h, 0, format,
                     GL_UNSIGNED_BYTE, pixels);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    return static_cast<uintptr_t>(tex);
}

void GpuTileUploader::doFree(uintptr_t handle)
{
    if (handle == 0)
        return;
    if (m_free)
    {
        m_free(handle);
        return;
    }
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx || !ctx->isValid())
        return;
    QOpenGLFunctions *gl = ctx->functions();
    if (!gl)
        return;
    GLuint tex = static_cast<GLuint>(handle);
    gl->glDeleteTextures(1, &tex);
}
