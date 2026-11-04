#include "core/image/yuv/YuvExtensionMap.h"

#include "core/image/yuv/YuvFormat.h"

#include <algorithm>
#include <memory>
#include <mutex>

namespace mviewer::core::yuv
{
namespace
{

struct Table
{
    std::vector<ExtensionRule> rules;
    std::vector<std::string> extensions; // sorted, includes yuv + y4m
    std::string fingerprint;
};

std::shared_ptr<const Table> build(std::vector<ExtensionRule> rules)
{
    auto t = std::make_shared<Table>();
    t->rules = std::move(rules);
    t->extensions = {"y4m", "yuv"};
    for (const ExtensionRule &r : t->rules)
    {
        if (std::find(t->extensions.begin(), t->extensions.end(), r.ext) == t->extensions.end())
            t->extensions.push_back(r.ext);
        t->fingerprint += r.ext;
        t->fingerprint += '=';
        t->fingerprint += r.pixFmt;
        t->fingerprint += ';';
    }
    std::sort(t->extensions.begin(), t->extensions.end());
    return t;
}

std::vector<ExtensionRule> sanitize(const std::vector<ExtensionRule> &in)
{
    std::vector<ExtensionRule> out;
    for (const ExtensionRule &raw : in)
    {
        ExtensionRule r;
        r.ext = normalizeExtension(raw.ext);
        if (r.ext.empty())
            continue;
        if (!raw.pixFmt.empty())
        {
            Format f;
            if (!applyPixFmtName(raw.pixFmt, f))
                continue;
            r.pixFmt = pixFmtName(f);
            if (r.pixFmt.empty())
                continue;
        }
        const auto same = [&r](const ExtensionRule &e) { return e.ext == r.ext; };
        const auto it = std::find_if(out.begin(), out.end(), same);
        if (it != out.end())
            *it = r;
        else
            out.push_back(r);
    }
    return out;
}

std::mutex g_mutex;
std::shared_ptr<const Table> g_table;

std::shared_ptr<const Table> current()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_table)
        g_table = build(sanitize(defaultExtensionRules()));
    return g_table;
}

} // namespace

std::vector<ExtensionRule> defaultExtensionRules()
{
    return {
        {"yuv", ""},         {"nv12", "nv12"},   {"nv21", "nv21"},    {"i420", "yuv420p"},
        {"yv12", "yv12"},    {"p010", "p010le"}, {"yuyv", "yuyv422"}, {"yuy2", "yuyv422"},
        {"uyvy", "uyvy422"}, {"nv16", "nv16"},   {"gray", "gray"},    {"y", "gray"},
    };
}

std::string normalizeExtension(std::string_view text)
{
    while (!text.empty() && (text.front() == '.' || text.front() == '*' || text.front() == ' '))
        text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ')
        text.remove_suffix(1);
    if (text.empty() || text.size() > 16)
        return {};
    std::string out;
    for (char c : text)
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok)
            return {};
        out.push_back(c);
    }
    if (out == "y4m")
        return {};
    return out;
}

std::vector<ExtensionRule> setExtensionRules(const std::vector<ExtensionRule> &rules)
{
    auto table = build(sanitize(rules));
    std::vector<ExtensionRule> installed = table->rules;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_table = std::move(table);
    return installed;
}

std::vector<ExtensionRule> extensionRules()
{
    return current()->rules;
}

std::vector<std::string> yuvExtensions()
{
    return current()->extensions;
}

bool isYuvExtension(std::string_view ext)
{
    std::string lowered;
    lowered.reserve(ext.size());
    for (char c : ext)
        lowered.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
    const auto table = current();
    return std::binary_search(table->extensions.begin(), table->extensions.end(), lowered);
}

std::optional<std::string> mappedPixFmt(std::string_view ext)
{
    const std::string key = normalizeExtension(ext);
    if (key.empty())
        return std::nullopt;
    const auto table = current();
    for (const ExtensionRule &r : table->rules)
    {
        if (r.ext == key)
        {
            if (r.pixFmt.empty())
                return std::nullopt;
            return r.pixFmt;
        }
    }
    return std::nullopt;
}

std::string extensionRulesFingerprint()
{
    return current()->fingerprint;
}

std::string formatExtensionRule(const ExtensionRule &rule)
{
    return rule.ext + "=" + rule.pixFmt;
}

std::optional<ExtensionRule> parseExtensionRule(std::string_view text)
{
    const auto trim = [](std::string_view t)
    {
        while (!t.empty() && (t.front() == ' ' || t.front() == '\t'))
            t.remove_prefix(1);
        while (!t.empty() && (t.back() == ' ' || t.back() == '\t'))
            t.remove_suffix(1);
        return t;
    };
    text = trim(text);
    const size_t eq = text.find('=');
    if (eq == std::string_view::npos)
        return std::nullopt;
    ExtensionRule r;
    r.ext = normalizeExtension(trim(text.substr(0, eq)));
    if (r.ext.empty())
        return std::nullopt;
    const std::string_view pix = trim(text.substr(eq + 1));
    if (!pix.empty())
    {
        Format f;
        if (!applyPixFmtName(pix, f))
            return std::nullopt;
        r.pixFmt = pixFmtName(f);
        if (r.pixFmt.empty())
            return std::nullopt;
    }
    return r;
}

} // namespace mviewer::core::yuv
