#include "core/workspace/WorkspaceSerializer.h"

#include <cctype>
#include <cstdio>
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

std::string serializeWorkspace(const mviewer::domain::Workspace &ws)
{
    std::ostringstream os;
    os << "{\"root\":";
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
    os << "}";
    return os.str();
}

bool deserializeWorkspace(const std::string &text, mviewer::domain::Workspace &out)
{
    Parser p(text);
    if (!p.eat('{'))
        return false;
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

} // namespace mviewer::core
