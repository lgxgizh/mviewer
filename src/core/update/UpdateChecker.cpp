#include "core/update/UpdateChecker.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace mviewer::core
{

namespace
{

// Split "v1.2.3" → {1, 2, 3}.  Non-semver inputs return empty.
std::vector<int> parseSemver(const std::string &tag)
{
    std::vector<int> parts;
    if (tag.empty())
        return parts;
    size_t i = (tag[0] == 'v' || tag[0] == 'V') ? 1 : 0;
    std::string num;
    for (; i < tag.size(); ++i)
    {
        if (std::isdigit(static_cast<unsigned char>(tag[i])))
            num += tag[i];
        else if (tag[i] == '.' && !num.empty())
        {
            parts.push_back(std::stoi(num));
            num.clear();
        }
        else
            return {}; // invalid char
    }
    if (!num.empty())
        parts.push_back(std::stoi(num));
    return parts.size() >= 2 ? parts : std::vector<int>{};
}

// Return true if lhs semver is strictly greater than rhs.
bool isNewer(const std::string &lhsTag, const std::string &rhsTag)
{
    auto l = parseSemver(lhsTag);
    auto r = parseSemver(rhsTag);
    if (l.empty() || r.empty())
        return false;
    for (size_t i = 0; i < std::min(l.size(), r.size()); ++i)
    {
        if (l[i] != r[i])
            return l[i] > r[i];
    }
    return l.size() > r.size();
}

// Minimal HTTP GET wrapper that reads a URL and returns the body.
// Uses platform-specific code paths: WinHTTP on Windows, POSIX otherwise.
std::string httpGet(const std::string &url, std::string &error)
{
#if defined(_WIN32)
    // In a real product this would use WinHTTP or Qt's QNetworkAccessManager.
    // For P2 product polish, we provide a stub that the UI layer can replace
    // with QNetworkAccessManager::get().
    error = "UpdateChecker: platform HTTP not implemented; use Qt QNetworkAccessManager";
    (void)url;
    return {};
#else
    error = "UpdateChecker: HTTP not supported on this platform";
    (void)url;
    return {};
#endif
}

// Crude JSON extraction: find "tag_name":"value" in raw text.
std::string extractTagName(const std::string &json)
{
    const std::string key = "\"tag_name\"";
    auto pos = json.find(key);
    if (pos == std::string::npos)
        return {};
    pos = json.find('"', pos + key.size());
    if (pos == std::string::npos)
        return {};
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos)
        return {};
    size_t end = json.find('"', pos + 1);
    if (end == std::string::npos)
        return {};
    return json.substr(pos + 1, end - pos - 1);
}

} // namespace

UpdateChecker::UpdateChecker(std::string currentVersion)
    : m_currentVersion(std::move(currentVersion))
{
}

void UpdateChecker::checkGitHub(const std::string &repo, UpdateCallback cb)
{
    const std::string url = "https://api.github.com/repos/" + repo + "/releases/latest";
    checkUrl(url, std::move(cb));
}

void UpdateChecker::checkUrl(const std::string &updateJsonUrl, UpdateCallback cb)
{
    UpdateInfo info;
    info.currentVersion = m_currentVersion;

    std::string error;
    const std::string body = httpGet(updateJsonUrl, error);
    if (!error.empty())
    {
        info.error = error;
        if (cb)
            cb(info);
        return;
    }

    std::string latest = extractTagName(body);
    if (latest.empty())
    {
        info.error = "UpdateChecker: could not parse tag_name from response";
        if (cb)
            cb(info);
        return;
    }

    info.latestVersion = latest;
    info.hasUpdate = isNewer(latest, m_currentVersion);
    info.releaseUrl =
        "https://github.com/" +
        updateJsonUrl.substr(updateJsonUrl.find("repos/") + 6,
                             updateJsonUrl.find("/releases") - updateJsonUrl.find("repos/") - 6) +
        "/releases/tag/" + latest;

    if (cb)
        cb(info);
}

} // namespace mviewer::core
