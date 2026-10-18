// M17: ExportManager implementation — preset I/O and singleton lifecycle.
#include "ExportManager.h"
#include "core/filesystem/Utf8Path.h"

#include <QByteArrayView>
#include <QChar>
#include <QSaveFile>
#include <QString>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <cstring>
#endif

// Lightweight JSON building — no Qt dependency in core layer.
namespace
{

#ifdef _WIN32
std::string envUtf8(const wchar_t *name)
{
    const wchar_t *value = _wgetenv(name);
    return value ? mviewer::core::pathToUtf8(std::filesystem::path(value)) : std::string();
}
#endif

std::string qs(const std::string &v)
{
    // Minimal JSON string quoting.
    std::ostringstream os;
    os << '"';
    for (char c : v)
    {
        if (c == '"' || c == '\\')
            os << '\\' << c;
        else if (c == '\n')
            os << "\\n";
        else if (c == '\r')
            os << "\\r";
        else if (c == '\t')
            os << "\\t";
        else
            os << c;
    }
    os << '"';
    return os.str();
}

int parseInt(const std::string &s, int fallback = 0)
{
    try
    {
        return std::stoi(s);
    }
    catch (...)
    {
        return fallback;
    }
}

// Crude JSON token reader: reads the first string value for a known key.
std::string extractStr(const std::string &json, const std::string &key)
{
    const std::string target = '"' + key + '"';
    auto pos = json.find(target);
    if (pos == std::string::npos)
        return {};
    // skip key + ":"
    pos += target.size();
    while (pos < json.size() && (json[pos] == ':' || json[pos] == ' ' || json[pos] == '\t'))
        ++pos;
    if (pos >= json.size() || json[pos] != '"')
        return {};
    ++pos; // skip opening quote
    std::string val;
    while (pos < json.size() && json[pos] != '"')
    {
        if (json[pos] == '\\' && pos + 1 < json.size())
        {
            ++pos;
            switch (json[pos])
            {
            case 'n':
                val += '\n';
                break;
            case 'r':
                val += '\r';
                break;
            case 't':
                val += '\t';
                break;
            default:
                val += json[pos];
                break;
            }
        }
        else
            val += json[pos];
        ++pos;
    }
    return val;
}

// Preset names become a single path component. Letters (including CJK),
// digits, space, '-' and '_' stay; every other code point, including
// separators and "..", is replaced so the name cannot escape the preset dir.
std::string sanitizePresetName(const std::string &name)
{
    if (name.empty() || name.size() > static_cast<size_t>(std::numeric_limits<qsizetype>::max()))
        return {};
    const QString in =
        QString::fromUtf8(QByteArrayView(name.data(), static_cast<qsizetype>(name.size())));
    QString out;
    const QList<uint> points = in.toUcs4();
    out.reserve(points.size());
    for (uint point : points)
    {
        const char32_t cp = static_cast<char32_t>(point);
        if (QChar::isLetter(cp) || QChar::isDigit(cp) || cp == U' ' || cp == U'-' || cp == U'_')
            out.append(QString::fromUcs4(&cp, 1));
        else
            out.append(QLatin1Char('_'));
    }
    const QByteArray utf8 = out.toUtf8();
    if (utf8.isEmpty())
        return {};
    return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

bool writePresetFile(const std::string &path, const std::string &json)
{
    if (path.empty() || json.empty() ||
        path.size() > static_cast<size_t>(std::numeric_limits<qsizetype>::max()) ||
        json.size() > static_cast<size_t>(std::numeric_limits<qint64>::max()))
        return false;
    QSaveFile out(
        QString::fromUtf8(QByteArrayView(path.data(), static_cast<qsizetype>(path.size()))));
    if (!out.open(QIODevice::WriteOnly))
        return false;
    const auto bytes = static_cast<qint64>(json.size());
    if (out.write(json.data(), bytes) != bytes)
        return false;
    return out.commit();
}

int extractInt(const std::string &json, const std::string &key, int fallback = 0)
{
    const std::string target = '"' + key + '"';
    auto pos = json.find(target);
    if (pos == std::string::npos)
        return fallback;
    pos += target.size();
    while (pos < json.size() && (json[pos] == ':' || json[pos] == ' ' || json[pos] == '\t'))
        ++pos;
    if (pos >= json.size())
        return fallback;
    auto end = pos;
    while (end < json.size() && (std::isdigit(json[end]) || json[end] == '-'))
        ++end;
    return parseInt(json.substr(pos, end - pos), fallback);
}

} // namespace

ExportManager &ExportManager::instance()
{
    static ExportManager inst;
    return inst;
}

std::string ExportManager::Preset::toJson() const
{
    std::ostringstream os;
    os << "{\n";
    os << "  \"name\": " << qs(name) << ",\n";
    os << "  \"outDir\": " << qs(outDir) << ",\n";
    os << "  \"format\": " << qs(format) << ",\n";
    os << "  \"quality\": " << quality << ",\n";
    os << "  \"resizeMode\": " << qs(resizeMode) << ",\n";
    os << "  \"resizeValue\": " << resizeValue << ",\n";
    os << "  \"watermarkText\": " << qs(watermarkText) << ",\n";
    os << "  \"watermarkPos\": " << watermarkPos << ",\n";
    os << "  \"watermarkOpacity\": " << watermarkOpacity << ",\n";
    os << "  \"renamePattern\": " << qs(renamePattern) << ",\n";
    os << "  \"cols\": " << cols << ",\n";
    os << "  \"thumbSize\": " << thumbSize << "\n";
    os << "}\n";
    return os.str();
}

ExportManager::Preset ExportManager::Preset::fromJson(const std::string &json)
{
    Preset p;
    p.name = extractStr(json, "name");
    p.outDir = extractStr(json, "outDir");
    p.format = extractStr(json, "format");
    if (p.format.empty())
        p.format = "jpeg";
    p.quality = extractInt(json, "quality", 90);
    p.resizeMode = extractStr(json, "resizeMode");
    p.resizeValue = extractInt(json, "resizeValue", 1920);
    p.watermarkText = extractStr(json, "watermarkText");
    p.watermarkPos = extractInt(json, "watermarkPos", 0);
    p.watermarkOpacity = extractInt(json, "watermarkOpacity", 40);
    p.renamePattern = extractStr(json, "renamePattern");
    p.cols = extractInt(json, "cols", 4);
    p.thumbSize = extractInt(json, "thumbSize", 200);
    return p;
}

std::string ExportManager::defaultPresetDir()
{
#ifdef _WIN32
    const std::string appData = envUtf8(L"APPDATA");
    if (!appData.empty())
        return appData + "\\mviewer\\presets";
    const std::string localAppData = envUtf8(L"LOCALAPPDATA");
    if (!localAppData.empty())
        return localAppData + "\\mviewer\\presets";
    return "presets";
#else
    const char *home = std::getenv("HOME");
    std::string base = home ? home : ".";
    return base + "/.mviewer/presets";
#endif
}

std::string ExportManager::presetPath(const std::string &name)
{
    const std::string safe = sanitizePresetName(name);
    if (safe.empty())
        return {};
    return defaultPresetDir() + "/" + safe + ".json";
}

bool ExportManager::savePreset(const Preset &p)
{
    Preset stored = p;
    stored.name = sanitizePresetName(p.name);
    if (stored.name.empty())
        return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    std::error_code ec;
    const auto dir = mviewer::core::pathFromUtf8(defaultPresetDir());
    std::filesystem::create_directories(dir, ec);
    if (ec)
        return false;

    const std::string path = presetPath(stored.name);
    if (!writePresetFile(path, stored.toJson()))
        return false;

    auto it = std::find_if(m_presets.begin(), m_presets.end(),
                           [&](const Preset &x) { return x.name == stored.name; });
    if (it != m_presets.end())
        *it = stored;
    else
        m_presets.push_back(stored);
    return true;
}

bool ExportManager::deletePreset(const std::string &name)
{
    const std::string safe = sanitizePresetName(name);
    if (safe.empty())
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_presets.erase(std::remove_if(m_presets.begin(), m_presets.end(),
                                   [&](const Preset &x) { return x.name == safe; }),
                    m_presets.end());
    std::error_code ec;
    const std::string path = presetPath(safe);
    if (path.empty())
        return false;
    return std::filesystem::remove(mviewer::core::pathFromUtf8(path), ec) || !ec;
}

std::vector<ExportManager::Preset> ExportManager::listPresets() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_presets;
}

ExportManager::Preset ExportManager::loadPreset(const std::string &name) const
{
    const std::string path = presetPath(name);
    if (path.empty())
        return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ifstream in(mviewer::core::pathFromUtf8(path), std::ios::binary);
    if (!in)
        return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    Preset loaded = Preset::fromJson(ss.str());
    loaded.name = sanitizePresetName(loaded.name.empty() ? name : loaded.name);
    return loaded;
}

void ExportManager::loadAllPresets()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_presets.clear();
    std::error_code ec;
    const auto dir = mviewer::core::pathFromUtf8(defaultPresetDir());
    if (!std::filesystem::exists(dir, ec))
        return;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc) && !fileEc &&
            mviewer::core::pathToUtf8(it->path().extension()) == ".json")
        {
            std::ifstream in(it->path(), std::ios::binary);
            if (!in)
                continue;
            std::ostringstream ss;
            ss << in.rdbuf();
            auto p = Preset::fromJson(ss.str());
            p.name = sanitizePresetName(p.name);
            if (!p.name.empty())
                m_presets.push_back(std::move(p));
        }
    }
}

void ExportManager::saveAllPresets()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::error_code ec;
    const auto dir = mviewer::core::pathFromUtf8(defaultPresetDir());
    std::filesystem::create_directories(dir, ec);
    if (ec)
        return;
    for (auto &p : m_presets)
    {
        p.name = sanitizePresetName(p.name);
        if (p.name.empty())
            continue;
        const std::string path = presetPath(p.name);
        (void)writePresetFile(path, p.toJson());
    }
}
