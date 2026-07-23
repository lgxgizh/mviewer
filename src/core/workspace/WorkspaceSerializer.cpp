#include "core/workspace/WorkspaceSerializer.h"

#include <cctype>
#include <cstdio>
#include <optional>
#include <sstream>

namespace mviewer::core
{

namespace
{

void esc(std::ostringstream &os, const std::string &s)
{
    os << '"';
    for (char c : s)
    {
        switch (c)
        {
        case '"':
            os << "\\\"";
            break;
        case '\\':
            os << "\\\\";
            break;
        case '\n':
            os << "\\n";
            break;
        case '\r':
            os << "\\r";
            break;
        case '\t':
            os << "\\t";
            break;
        default:
            os << c;
            break;
        }
    }
    os << '"';
}

// --- Minimal JSON parser for the shape emitted by serializeWorkspace() ---
// Supports: objects, arrays, strings, integers, nested structure. Whitespace
// tolerant. Returns false on structural mismatch.
struct Parser
{
    const std::string &s;
    size_t i = 0;
    explicit Parser(const std::string &text) : s(text)
    {
    }

    void skipws()
    {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
    }

    bool eat(char c)
    {
        skipws();
        if (i < s.size() && s[i] == c)
        {
            ++i;
            return true;
        }
        return false;
    }

    bool peek(char c)
    {
        skipws();
        return i < s.size() && s[i] == c;
    }

    // Non-consuming string peek: returns the next string token's CONTENT
    // (without quotes) WITHOUT advancing the parser position.
    std::string peekString() const
    {
        size_t j = i;
        while (j < s.size() && std::isspace(static_cast<unsigned char>(s[j])))
            ++j;
        if (j >= s.size() || s[j] != '"')
            return "";
        ++j;
        std::string result;
        while (j < s.size() && s[j] != '"')
        {
            if (s[j] == '\\' && j + 1 < s.size())
            {
                ++j;
                switch (s[j])
                {
                case 'n':
                    result += '\n';
                    break;
                case 't':
                    result += '\t';
                    break;
                case 'r':
                    result += '\r';
                    break;
                default:
                    result += s[j];
                    break;
                }
            }
            else
                result += s[j];
            ++j;
        }
        return result;
    }

    std::string parseString()
    {
        std::string out;
        if (!eat('"'))
            return out;
        while (i < s.size() && s[i] != '"')
        {
            if (s[i] == '\\' && i + 1 < s.size())
            {
                ++i;
                switch (s[i])
                {
                case 'n':
                    out += '\n';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'r':
                    out += '\r';
                    break;
                default:
                    out += s[i];
                    break;
                }
            }
            else
            {
                out += s[i];
            }
            ++i;
        }
        eat('"');
        return out;
    }

    long long parseNumber()
    {
        skipws();
        size_t start = i;
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-'))
            i++;
        return std::strtoll(s.substr(start, i - start).c_str(), nullptr, 10);
    }

    double parseDouble()
    {
        skipws();
        size_t start = i;
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' ||
                                s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E'))
            i++;
        return std::strtod(s.substr(start, i - start).c_str(), nullptr);
    }

    // Parse a value that we expect to be a string at `key` within the current
    // object; advances past it. Returns true if the key was found and a string
    // consumed.
    bool memberStr(const std::string &key, std::string &out)
    {
        skipws();
        if (i < s.size() && s[i] == '}')
            return false;
        std::string k = parseString();
        if (k != key)
            return false;
        if (!eat(':'))
            return false;
        out = parseString();
        return true;
    }

    bool memberNum(const std::string &key, long long &out)
    {
        skipws();
        if (i < s.size() && s[i] == '}')
            return false;
        std::string k = parseString();
        if (k != key)
            return false;
        if (!eat(':'))
            return false;
        out = parseNumber();
        return true;
    }
};

} // namespace

// --- CompareSession (M15 P0#1): embedded so Workspace stays a flat value type. ---
// Shape: {"imageIds":[...],"cells":[[s,ox,oy],...],"syncMode":N,"blink":I,
//         "sharedScale":S,"sharedOffsetX":X,"sharedOffsetY":Y,
//         "cols":C,"rows":R,"selection":[x,y,w,h,sync],
//         "threshold":T,"blinkIntervalMs":B,"sidePanel":0|1,"layoutIndex":L}
std::string serializeCompareSession(const mviewer::domain::CompareSession &s)
{
    std::ostringstream os;
    os << "{\"imageIds\":[";
    for (size_t i = 0; i < s.imageIds.size(); ++i)
    {
        if (i)
            os << ',';
        esc(os, s.imageIds[i]);
    }
    os << "],\"cells\":[";
    for (size_t i = 0; i < s.cells.size(); ++i)
    {
        if (i)
            os << ',';
        os << '[' << s.cells[i].scale << ',' << s.cells[i].offsetX << ',' << s.cells[i].offsetY
           << ']';
    }
    os << "],\"syncMode\":" << static_cast<int>(s.syncMode) << ",\"blink\":" << s.blinkIndex
       << ",\"sharedScale\":" << s.sharedScale << ",\"sharedOffsetX\":" << s.sharedOffsetX
       << ",\"sharedOffsetY\":" << s.sharedOffsetY << ",\"cols\":" << s.cols
       << ",\"rows\":" << s.rows << ",\"selection\":[" << s.selection.x << ',' << s.selection.y
       << ',' << s.selection.w << ',' << s.selection.h << ',' << (s.selection.synced ? 1 : 0)
       << "],\"threshold\":" << static_cast<int>(s.threshold)
       << ",\"blinkIntervalMs\":" << s.blinkIntervalMs
       << ",\"sidePanel\":" << (s.sidePanelVisible ? 1 : 0) << ",\"layoutIndex\":" << s.layoutIndex
       << "}";
    return os.str();
}

bool parseCompareSession(const std::string &text, mviewer::domain::CompareSession &out)
{
    Parser p(text);
    if (!p.eat('{'))
        return false;
    long long syncMode = 0, blink = -1, cols = 0, rows = 0;
    double sharedScale = 1.0, sharedOffsetX = 0.0, sharedOffsetY = 0.0;
    int sx = 0, sy = 0, sw = 0, sh = 0, ssync = 0;
    long long threshold = 0, blinkIntervalMs = 500, sidePanel = 0, layoutIndex = 0;
    bool haveIds = false, haveCells = false;
    while (!p.peek('}'))
    {
        if (!out.imageIds.empty() || haveIds || haveCells)
            p.eat(',');
        const std::string k = p.parseString();
        if (!p.eat(':'))
            return false;
        if (k == "imageIds")
        {
            haveIds = true;
            if (!p.eat('['))
                return false;
            while (!p.eat(']'))
            {
                if (!out.imageIds.empty())
                    p.eat(',');
                out.imageIds.push_back(p.parseString());
            }
        }
        else if (k == "cells")
        {
            haveCells = true;
            if (!p.eat('['))
                return false;
            while (!p.eat(']'))
            {
                if (!out.cells.empty())
                    p.eat(',');
                if (!p.eat('['))
                    return false;
                mviewer::domain::CellTransform ct;
                ct.scale = static_cast<double>(p.parseDouble());
                p.eat(',');
                ct.offsetX = static_cast<double>(p.parseDouble());
                p.eat(',');
                ct.offsetY = static_cast<double>(p.parseDouble());
                if (!p.eat(']'))
                    return false;
                out.cells.push_back(ct);
            }
        }
        else if (k == "syncMode")
            syncMode = p.parseNumber();
        else if (k == "blink")
            blink = p.parseNumber();
        else if (k == "sharedScale")
            sharedScale = p.parseDouble();
        else if (k == "sharedOffsetX")
            sharedOffsetX = p.parseDouble();
        else if (k == "sharedOffsetY")
            sharedOffsetY = p.parseDouble();
        else if (k == "cols")
            cols = p.parseNumber();
        else if (k == "rows")
            rows = p.parseNumber();
        else if (k == "selection")
        {
            if (!p.eat('['))
                return false;
            sx = static_cast<int>(p.parseNumber());
            p.eat(',');
            sy = static_cast<int>(p.parseNumber());
            p.eat(',');
            sw = static_cast<int>(p.parseNumber());
            p.eat(',');
            sh = static_cast<int>(p.parseNumber());
            p.eat(',');
            ssync = static_cast<int>(p.parseNumber());
            if (!p.eat(']'))
                return false;
        }
        else if (k == "threshold")
            threshold = p.parseNumber();
        else if (k == "blinkIntervalMs")
            blinkIntervalMs = p.parseNumber();
        else if (k == "sidePanel")
            sidePanel = p.parseNumber();
        else if (k == "layoutIndex")
            layoutIndex = p.parseNumber();
        else
        {
            // Unknown key: skip a scalar/string value to stay forward-tolerant.
            p.parseString();
        }
    }
    if (!p.eat('}'))
        return false;
    out.syncMode = static_cast<mviewer::domain::SyncMode>(syncMode);
    out.blinkIndex = static_cast<int>(blink);
    out.sharedScale = sharedScale;
    out.sharedOffsetX = sharedOffsetX;
    out.sharedOffsetY = sharedOffsetY;
    out.cols = static_cast<int>(cols);
    out.rows = static_cast<int>(rows);
    // CompareSelection field order is {x,y,w,h,active,synced}. The JSON
    // stores [x,y,w,h,synced] (5 values), so map the 5th to synced and
    // derive active from a non-empty region.
    out.selection = {sx, sy, sw, sh, (sw > 0 && sh > 0), ssync != 0};
    out.threshold = static_cast<uint8_t>(threshold);
    out.blinkIntervalMs = static_cast<int>(blinkIntervalMs);
    out.sidePanelVisible = (sidePanel != 0);
    out.layoutIndex = static_cast<int>(layoutIndex);
    return haveIds; // require at least the image list to be a valid session
}

// M15: workspace version for forward/backward compatibility.
// Version 1 = legacy (no version field). Version 2 = current.
static constexpr int kWorkspaceVersion = 2;

std::string serializeWorkspace(const mviewer::domain::Workspace &ws)
{
    std::ostringstream os;
    os << "{\"version\":" << kWorkspaceVersion;
    os << ",\"root\":";
    esc(os, ws.rootPath);
    os << ",\"folders\":[";
    for (size_t f = 0; f < ws.folders.size(); ++f)
    {
        const auto &folder = ws.folders[f];
        if (f)
            os << ',';
        os << "{\"path\":";
        esc(os, folder.path);
        os << ",\"name\":";
        esc(os, folder.name);
        os << ",\"images\":[";
        for (size_t im = 0; im < folder.imageSet.images.size(); ++im)
        {
            const auto &m = folder.imageSet.images[im];
            if (im)
                os << ',';
            os << "{\"filePath\":";
            esc(os, m.filePath);
            os << ",\"fileName\":";
            esc(os, m.fileName);
            os << ",\"width\":" << m.width << ",\"height\":" << m.height;
            // M12.1 session persistence: ROI (pixel coords) + analysis text.
            os << ",\"roi\":[" << m.roiX << ',' << m.roiY << ',' << m.roiW << ',' << m.roiH << ']';
            os << ",\"analysis\":";
            esc(os, m.analysis);
            os << '}';
        }
        os << "]}";
    }
    os << "]";
    // M12.2 (review fix): persist the explicit compare-session image list so a
    // compare session with no ROI/analysis is still restored on reopen.
    os << ",\"comparedImages\":[";
    for (size_t ci = 0; ci < ws.comparedImages.size(); ++ci)
    {
        if (ci)
            os << ',';
        esc(os, ws.comparedImages[ci]);
    }
    os << ']';
    // M15: embed the serialized CompareSession snapshot (sync mode, zoom/pan,
    // shared transform, ROI) so reopening restores the full compare view.
    os << ",\"compareSession\":";
    esc(os, ws.compareSessionJson);
    os << "}";
    return os.str();
}

bool parseWorkspace(const std::string &text, mviewer::domain::Workspace &out)
{
    Parser p(text);
    if (!p.eat('{'))
        return false;

    // M15: version-aware deserialization. Version 1 (legacy) has no "version" field;
    // version 2+ starts with "version":N. Detect by peeking at the first key.
    std::string firstKey = p.peekString();
    if (firstKey == "version")
    {
        p.parseString(); // consume "version"
        p.eat(':');
        int version = static_cast<int>(p.parseNumber());
        p.eat(',');
        // Future: handle migration from version 1 -> 2 here.
        (void)version;
    }

    if (!p.memberStr("root", out.rootPath))
        return false;
    if (!p.eat(','))
        return false;
    if (p.parseString() != "folders" || !p.eat(':'))
        return false;
    if (!p.eat('['))
        return false;

    out.folders.clear();
    while (!p.eat(']'))
    {
        if (!out.folders.empty())
            p.eat(',');
        mviewer::domain::Folder folder;
        if (!p.eat('{'))
            return false;
        if (!p.memberStr("path", folder.path))
            return false;
        if (!p.eat(','))
            return false;
        if (!p.memberStr("name", folder.name))
            return false;
        if (!p.eat(','))
            return false;
        if (p.parseString() != "images" || !p.eat(':'))
            return false;
        if (!p.eat('['))
            return false;
        while (!p.eat(']'))
        {
            if (!folder.imageSet.images.empty())
                p.eat(',');
            mviewer::domain::ImageMetadata m;
            if (!p.eat('{'))
                return false;
            if (!p.memberStr("filePath", m.filePath))
                return false;
            if (!p.eat(','))
                return false;
            if (!p.memberStr("fileName", m.fileName))
                return false;
            if (!p.eat(','))
                return false;
            long long w = 0, h = 0;
            if (!p.memberNum("width", w))
                return false;
            if (!p.eat(','))
                return false;
            if (!p.memberNum("height", h))
                return false;
            m.width = static_cast<int>(w);
            m.height = static_cast<int>(h);
            // M12.1: optional roi + analysis (absent in older files).
            if (p.peek(','))
            {
                p.eat(',');
                if (p.parseString() != "roi" || !p.eat(':') || !p.eat('['))
                    return false;
                m.roiX = static_cast<int>(p.parseNumber());
                p.eat(',');
                m.roiY = static_cast<int>(p.parseNumber());
                p.eat(',');
                m.roiW = static_cast<int>(p.parseNumber());
                p.eat(',');
                m.roiH = static_cast<int>(p.parseNumber());
                p.eat(']');
                if (!p.eat(','))
                    return false;
                if (p.parseString() != "analysis" || !p.eat(':'))
                    return false;
                m.analysis = p.parseString();
            }
            folder.imageSet.images.push_back(m);
            p.eat('}');
        }
        p.eat('}');
        out.folders.push_back(folder);
    }
    // M12.2 (review fix): optional explicit compare-session image list.
    // Absent in legacy files, so tolerate its absence.
    if (p.peek(','))
    {
        p.eat(',');
        if (p.parseString() == "comparedImages" && p.eat(':'))
        {
            if (!p.eat('['))
                return false;
            while (!p.eat(']'))
            {
                if (!out.comparedImages.empty())
                    p.eat(',');
                out.comparedImages.push_back(p.parseString());
            }
        }
        else if (p.parseString() == "compareSession" && p.eat(':'))
        {
            out.compareSessionJson = p.parseString();
        }
    }
    // M15: also tolerate the case where compareSession appears after
    // comparedImages (order-independent; only one optional comma consumed above).
    if (p.peek(','))
    {
        p.eat(',');
        if (p.parseString() == "compareSession" && p.eat(':'))
            out.compareSessionJson = p.parseString();
        else if (p.parseString() == "comparedImages" && p.eat(':'))
        {
            if (!p.eat('['))
                return false;
            while (!p.eat(']'))
            {
                if (!out.comparedImages.empty())
                    p.eat(',');
                out.comparedImages.push_back(p.parseString());
            }
        }
    }
    p.eat('}'); // close root object
    return true;
}

void RecentFiles::add(const std::string &path)
{
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i] == path)
        {
            // move to front
            for (size_t j = i; j > 0; --j)
                m_items[j] = m_items[j - 1];
            m_items[0] = path;
            return;
        }
    }
    m_items.insert(m_items.begin(), path);
    while (m_items.size() > m_max)
        m_items.pop_back();
}

std::string RecentFiles::serialize() const
{
    std::ostringstream os;
    os << "{\"recent\":[";
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (i)
            os << ',';
        esc(os, m_items[i]);
    }
    os << "]}";
    return os.str();
}

bool RecentFiles::deserialize(const std::string &text)
{
    Parser p(text);
    if (!p.eat('{'))
        return false;
    if (p.parseString() != "recent" || !p.eat(':'))
        return false;
    if (!p.eat('['))
        return false;
    m_items.clear();
    while (!p.eat(']'))
    {
        if (!m_items.empty())
            p.eat(',');
        m_items.push_back(p.parseString());
    }
    return true;
}

// M15 (review follow-up): caller-facing entry points return std::optional and
// drop the bool + out-param style. The robust parsers above stay bool+out
// internally; these thin wrappers only convert to std::optional.
std::optional<mviewer::domain::Workspace> deserializeWorkspace(const std::string &text)
{
    mviewer::domain::Workspace ws;
    if (parseWorkspace(text, ws))
        return ws;
    return std::nullopt;
}

std::optional<mviewer::domain::CompareSession> deserializeCompareSession(const std::string &text)
{
    mviewer::domain::CompareSession s;
    if (parseCompareSession(text, s))
        return s;
    return std::nullopt;
}

} // namespace mviewer::core
