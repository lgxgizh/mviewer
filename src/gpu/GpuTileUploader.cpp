#include "gpu/GpuTileUploader.h"

#include <QByteArray>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSettings>
#include <algorithm>
#include <cstdlib>
#include <cstring>

// ─── capability probes ───────────────────────────────────────────────────────

bool GpuTileUploader::available()
{
    // A current QOpenGLContext means we can issue GL calls (real Stage A host
    // or a test that made a context current). Headless QCoreApplication /
    // offscreen without GL returns false — CPU path stays the default.
    // Deliberately NOT cached: context availability changes per thread and per
    // test section.
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    return ctx != nullptr && ctx->isValid();
}

namespace
{
int gpuRequestedByEnvironment()
{
    const char *env = std::getenv("MVIEWER_GPU");
    if (!env || env[0] == '\0')
        return -1;
    if (std::strcmp(env, "0") == 0 || std::strcmp(env, "false") == 0 ||
        std::strcmp(env, "FALSE") == 0 || std::strcmp(env, "no") == 0 ||
        std::strcmp(env, "off") == 0)
        return 0;
    return 1;
}

bool gpuEnabledBySettings()
{
    QSettings s;
    return s.value("gpuAcceleration", true).toBool();
}
} // namespace

bool GpuTileUploader::enabled()
{
    if (!available())
        return false;
    const int env = gpuRequestedByEnvironment();
    if (env >= 0)
        return env == 1;
    return gpuEnabledBySettings();
}

namespace
{
uint64_t fingerprintPixels(const uint8_t *pixels, int w, int h, int channels)
{
    if (!pixels || w <= 0 || h <= 0 || channels <= 0)
        return 0;
    const size_t n =
        static_cast<size_t>(w) * static_cast<size_t>(h) * static_cast<size_t>(channels);
    uint64_t hash = 14695981039346656037ull;
    const size_t step = std::max<size_t>(1, n / 32);
    for (size_t i = 0; i < n; i += step)
    {
        hash ^= pixels[i];
        hash *= 1099511628211ull;
    }
    hash ^= n;
    return hash == 0 ? 1 : hash;
}

size_t tileBytes(int w, int h, int channels)
{
    if (w <= 0 || h <= 0 || channels <= 0)
        return 0;
    return static_cast<size_t>(w) * static_cast<size_t>(h) * static_cast<size_t>(channels);
}
} // namespace

void GpuTileUploader::ensureBudgetLoaded()
{
    if (m_budgetLoaded)
        return;
    m_budgetLoaded = true;
    // Injected uploaders (unit tests) own maxBytes/maxResident directly.
    if (m_upload)
        return;
    int mb = 256;
    const char *env = std::getenv("MVIEWER_GPU_BUDGET_MB");
    if (env && env[0] != '\0')
        mb = std::atoi(env);
    else
    {
        QSettings s;
        if (s.contains(QStringLiteral("gpuTileBudgetMb")))
            mb = s.value(QStringLiteral("gpuTileBudgetMb")).toInt();
    }
    if (mb > 0)
        maxBytes = static_cast<size_t>(mb) * 1024ull * 1024ull;
}

// ─── residency ───────────────────────────────────────────────────────────────

bool GpuTileUploader::ensure(const TileKey &key, const uint8_t *pixels, int w, int h, int channels)
{
    // Injected callbacks (unit tests) always run; real GL path only when
    // enabled() so the CPU compositor remains the verified default.
    const bool useInjected = static_cast<bool>(m_upload);
    if (!useInjected && !enabled())
        return false;
    ensureBudgetLoaded();

    const uint64_t fp = fingerprintPixels(pixels, w, h, channels);
    const size_t bytes = tileBytes(w, h, channels);
    auto it = m_map.find(key);
    if (it != m_map.end())
    {
        const bool sameSize =
            it->second.w == w && it->second.h == h && it->second.channels == channels;
        // Null pixels or a matching sample: already resident, do not re-upload.
        if (sameSize && (pixels == nullptr || fp == it->second.fingerprint))
        {
            touch(key);
            return it->second.handle != 0;
        }
        if (sameSize && pixels && doReplace(it->second.handle, pixels, w, h, channels))
        {
            it->second.fingerprint = fp;
            touch(key);
            return true;
        }
        eraseEntry(it);
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
    e.w = w;
    e.h = h;
    e.channels = channels;
    e.bytes = bytes;
    e.fingerprint = fp == 0 ? 1 : fp;
    e.lruIt = std::prev(m_lru.end());
    m_bytes += e.bytes;
    m_map.emplace(key, e);
    evictIfNeeded();
    return m_map.find(key) != m_map.end();
}

void GpuTileUploader::pinVisible(const TileKey *keys, size_t count)
{
    m_pinned.clear();
    if (!keys || count == 0)
        return;
    for (size_t i = 0; i < count; ++i)
    {
        m_pinned.insert(keys[i]);
        touch(keys[i]);
    }
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
    m_pinned.clear();
    m_bytes = 0;
}

void GpuTileUploader::eraseEntry(std::unordered_map<TileKey, Entry, TileKeyHash>::iterator it)
{
    if (it == m_map.end())
        return;
    if (m_bytes >= it->second.bytes)
        m_bytes -= it->second.bytes;
    else
        m_bytes = 0;
    m_lru.erase(it->second.lruIt);
    doFree(it->second.handle);
    m_pinned.erase(it->first);
    m_map.erase(it);
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
    auto overBudget = [this]()
    { return static_cast<int>(m_map.size()) > maxResident || m_bytes > maxBytes; };
    // Pass 1: drop tiles that are not on screen this frame.
    for (auto lruIt = m_lru.begin(); overBudget() && lruIt != m_lru.end();)
    {
        if (m_pinned.find(*lruIt) != m_pinned.end())
        {
            ++lruIt;
            continue;
        }
        auto it = m_map.find(*lruIt);
        lruIt = m_lru.erase(lruIt);
        if (it == m_map.end())
            continue;
        if (m_bytes >= it->second.bytes)
            m_bytes -= it->second.bytes;
        else
            m_bytes = 0;
        doFree(it->second.handle);
        m_map.erase(it);
    }
    // Pass 2: still over budget (every resident tile is pinned) — drop oldest.
    while (overBudget() && !m_lru.empty())
    {
        const TileKey oldest = m_lru.front();
        auto it = m_map.find(oldest);
        if (it == m_map.end())
        {
            m_lru.pop_front();
            continue;
        }
        eraseEntry(it);
    }
}

uintptr_t GpuTileUploader::doUpload(const TileKey &key, const uint8_t *pixels, int w, int h,
                                    int channels)
{
    ++m_uploads;
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

bool GpuTileUploader::doReplace(uintptr_t handle, const uint8_t *pixels, int w, int h, int channels)
{
    if (handle == 0 || !pixels || w <= 0 || h <= 0 || channels <= 0)
        return false;
    // Same-size content change keeps the texture object. Injected tests have
    // no pixel store; skipping the upload callback is the "no re-upload" path.
    if (m_upload)
        return true;

    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx || !ctx->isValid())
        return false;
    QOpenGLFunctions *gl = ctx->functions();
    if (!gl)
        return false;
    GLenum format = GL_RGBA;
    if (channels == 1)
        format = GL_RED;
    else if (channels == 3)
        format = GL_RGB;
    else if (channels != 4)
        return false;
    gl->glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(handle));
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, GL_UNSIGNED_BYTE, pixels);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void GpuTileUploader::setMagnifyNearest(uintptr_t handle, bool nearest)
{
    if (handle == 0 || m_upload)
        return;
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx || !ctx->isValid())
        return;
    QOpenGLFunctions *gl = ctx->functions();
    if (!gl)
        return;
    gl->glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(handle));
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
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
