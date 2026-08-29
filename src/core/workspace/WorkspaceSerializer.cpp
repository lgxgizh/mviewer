#include "core/workspace/WorkspaceSerializer.h"

#include <algorithm>
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
// Shape: {"imageIds":[...],"frameIndices":[...],"cells":[[s,ox,oy],...],"syncMode":N,"blink":I,
//         "sharedScale":S,"sharedOffsetX":X,"sharedOffsetY":Y,
//         "cols":C,"rows":R,"selection":[x,y,w,h,sync],
//         "threshold":T,"blinkIntervalMs":B,"sidePanel":0|1,"layoutIndex":L,
//         "customColumns":C,"uniformScale":0|1}
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
    os << "],\"frameIndices\":[";
    for (size_t i = 0; i < s.imageIds.size(); ++i)
    {
        if (i)
            os << ',';
        const int frame = i < s.frameIndices.size() ? std::max(0, s.frameIndices[i]) : 0;
        os << frame;
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
       << ",\"customColumns\":" << s.customColumns
       << ",\"uniformScale\":" << (s.uniformScale ? 1 : 0) << "}";
    return os.str();
}

struct CompareParseState
{
    long long syncMode = 0;
    long long blink = -1;
    long long cols = 0;
    long long rows = 0;
    double sharedScale = 1.0;
    double sharedOffsetX = 0.0;
    double sharedOffsetY = 0.0;
    int sx = 0;
    int sy = 0;
    int sw = 0;
    int sh = 0;
    int ssync = 0;
    long long threshold = 0;
    long long blinkIntervalMs = 500;
    long long sidePanel = 0;
    long long layoutIndex = 0;
    long long customColumns = 2;
    long long uniformScale = 0;
    bool haveIds = false;
    bool haveCells = false;
};

static bool parseCompareCollections(Parser &p, const std::string &key,
                                    mviewer::domain::CompareSession &out,
                                    CompareParseState &state)
{
    if (key == "imageIds")
    {
        state.haveIds = true;
        if (!p.eat('['))
            return false;
        while (!p.eat(']'))
        {
            if (!out.imageIds.empty())
                p.eat(',');
            out.imageIds.push_back(p.parseString());
        }
        return true;
    }
    if (key == "cells")
    {
        state.haveCells = true;
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
        return true;
    }
    if (key == "frameIndices")
    {
        if (!p.eat('['))
            return false;
        while (!p.eat(']'))
        {
            if (!out.frameIndices.empty())
                p.eat(',');
            out.frameIndices.push_back(static_cast<int>(p.parseNumber()));
        }
        return true;
    }
    if (key == "selection")
    {
        if (!p.eat('['))
            return false;
        state.sx = static_cast<int>(p.parseNumber());
        p.eat(',');
        state.sy = static_cast<int>(p.parseNumber());
        p.eat(',');
        state.sw = static_cast<int>(p.parseNumber());
        p.eat(',');
        state.sh = static_cast<int>(p.parseNumber());
        p.eat(',');
        state.ssync = static_cast<int>(p.parseNumber());
        return p.eat(']');
    }
    return false;
}

static bool parseCompareTransform(Parser &p, const std::string &key, CompareParseState &state)
{
    if (key == "syncMode")
        state.syncMode = p.parseNumber();
    else if (key == "blink")
        state.blink = p.parseNumber();
    else if (key == "sharedScale")
        state.sharedScale = p.parseDouble();
    else if (key == "sharedOffsetX")
        state.sharedOffsetX = p.parseDouble();
    else if (key == "sharedOffsetY")
        state.sharedOffsetY = p.parseDouble();
    else if (key == "cols")
        state.cols = p.parseNumber();
    else if (key == "rows")
        state.rows = p.parseNumber();
    else
        return false;
    return true;
}

static bool parseComparePresentation(Parser &p, const std::string &key,
                                     CompareParseState &state)
{
    if (key == "threshold")
        state.threshold = p.parseNumber();
    else if (key == "blinkIntervalMs")
        state.blinkIntervalMs = p.parseNumber();
    else if (key == "sidePanel")
        state.sidePanel = p.parseNumber();
    else if (key == "layoutIndex")
        state.layoutIndex = p.parseNumber();
    else if (key == "customColumns")
        state.customColumns = p.parseNumber();
    else if (key == "uniformScale")
        state.uniformScale = p.parseNumber();
    else
        return false;
    return true;
}

static void applyCompareParseState(const CompareParseState &state,
                                   mviewer::domain::CompareSession &out)
{
    out.syncMode = static_cast<mviewer::domain::SyncMode>(state.syncMode);
    out.blinkIndex = static_cast<int>(state.blink);
    out.sharedScale = state.sharedScale;
    out.sharedOffsetX = state.sharedOffsetX;
    out.sharedOffsetY = state.sharedOffsetY;
    out.cols = static_cast<int>(state.cols);
    out.rows = static_cast<int>(state.rows);
    out.selection = {state.sx, state.sy, state.sw, state.sh,
                     (state.sw > 0 && state.sh > 0), state.ssync != 0};
    out.threshold = static_cast<uint8_t>(state.threshold);
    out.blinkIntervalMs = static_cast<int>(state.blinkIntervalMs);
    out.sidePanelVisible = (state.sidePanel != 0);
    out.layoutIndex = static_cast<int>(state.layoutIndex);
    out.customColumns = static_cast<int>(state.customColumns);
    out.uniformScale = (state.uniformScale != 0);
}

bool parseCompareSession(const std::string &text, mviewer::domain::CompareSession &out)
{
    Parser p(text);
    if (!p.eat('{'))
        return false;
    CompareParseState state;
    while (!p.peek('}'))
    {
        if (!out.imageIds.empty() || state.haveIds || state.haveCells)
            p.eat(',');
        const std::string k = p.parseString();
        if (!p.eat(':'))
            return false;
        if (parseCompareCollections(p, k, out, state))
            continue;
        if (parseCompareTransform(p, k, state) || parseComparePresentation(p, k, state))
            continue;
        // Unknown key: skip a scalar/string value to stay forward-tolerant.
        p.parseString();
    }
    if (!p.eat('}'))
        return false;
    applyCompareParseState(state, out);
    if (out.frameIndices.size() < out.imageIds.size())
        out.frameIndices.resize(out.imageIds.size(), 0);
    if (out.frameIndices.size() > out.imageIds.size())
        out.frameIndices.resize(out.imageIds.size());
    for (auto &frame : out.frameIndices)
        frame = std::max(0, frame);
    return state.haveIds; // require at least the image list to be a valid session
}

// M15: workspace version for forward/backward compatibility.
// Version 1 = legacy (no version field). Version 2 added CompareSession;
// version 3 adds the optional per-image analysis producer id; version 4 adds
// the current frame/page and playback state.
static constexpr int kWorkspaceVersion = 4;

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
            os << ",\"analysisAnalyzerId\":";
            esc(os, m.analysisAnalyzerId);
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
    os << ",\"currentImagePath\":";
    esc(os, ws.currentImagePath);
    os << ",\"currentFrameIndex\":" << std::max(0, ws.currentFrameIndex);
    os << ",\"currentPlaying\":" << (ws.currentPlaying ? 1 : 0);
    os << "}";
    return os.str();
}

static bool parseWorkspaceImage(Parser &p, mviewer::domain::Folder &folder)
{
    mviewer::domain::ImageMetadata m;
    if (!p.eat('{') || !p.memberStr("filePath", m.filePath))
        return false;
    if (!p.eat(',') || !p.memberStr("fileName", m.fileName) || !p.eat(','))
        return false;
    long long w = 0;
    long long h = 0;
    if (!p.memberNum("width", w) || !p.eat(',') || !p.memberNum("height", h))
        return false;
    m.width = static_cast<int>(w);
    m.height = static_cast<int>(h);
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
        if (!p.eat(']') || !p.eat(',') || p.parseString() != "analysis" || !p.eat(':'))
            return false;
        m.analysis = p.parseString();
        if (p.peek(','))
        {
            p.eat(',');
            if (p.parseString() != "analysisAnalyzerId" || !p.eat(':'))
                return false;
            m.analysisAnalyzerId = p.parseString();
        }
    }
    folder.imageSet.images.push_back(m);
    p.eat('}');
    return true;
}

static bool parseWorkspaceFolder(Parser &p, mviewer::domain::Workspace &out)
{
    mviewer::domain::Folder folder;
    if (!p.eat('{') || !p.memberStr("path", folder.path))
        return false;
    if (!p.eat(',') || !p.memberStr("name", folder.name))
        return false;
    if (!p.eat(',') || p.parseString() != "images" || !p.eat(':') || !p.eat('['))
        return false;
    while (!p.eat(']'))
    {
        if (!folder.imageSet.images.empty())
            p.eat(',');
        if (!parseWorkspaceImage(p, folder))
            return false;
    }
    p.eat('}');
    out.folders.push_back(folder);
    return true;
}

static bool parseWorkspaceFolders(Parser &p, mviewer::domain::Workspace &out)
{
    if (!p.eat('['))
        return false;
    out.folders.clear();
    while (!p.eat(']'))
    {
        if (!out.folders.empty())
            p.eat(',');
        if (!parseWorkspaceFolder(p, out))
            return false;
    }
    return true;
}

static bool parseWorkspaceOptionalFields(Parser &p, mviewer::domain::Workspace &out)
{
    while (p.peek(','))
    {
        p.eat(',');
        const std::string key = p.parseString();
        if (!p.eat(':'))
            return false;
        if (key == "comparedImages")
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
        else if (key == "compareSession")
        {
            out.compareSessionJson = p.parseString();
        }
        else if (key == "currentImagePath")
        {
            out.currentImagePath = p.parseString();
        }
        else if (key == "currentFrameIndex")
        {
            out.currentFrameIndex = std::max(0, static_cast<int>(p.parseNumber()));
        }
        else if (key == "currentPlaying")
        {
            out.currentPlaying = p.parseNumber() != 0;
        }
        else
        {
            p.parseString();
        }
    }
    return true;
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
    if (!parseWorkspaceFolders(p, out) || !parseWorkspaceOptionalFields(p, out))
        return false;
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
