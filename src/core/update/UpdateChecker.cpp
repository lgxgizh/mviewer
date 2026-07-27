#include "core/update/UpdateChecker.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
// MViewer targets Windows; use the OS WinHTTP API for the update check so we
// don't pull in Qt6::Network. TLS 1.2/1.3 and timeouts are configured below.
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

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

// Convert a UTF-8 std::string to a wide string. This SDK's winhttp.h only
// declares the Unicode (W) entry points, so we go wide end-to-end.
static std::wstring toWide(const std::string &s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0)
        return {};
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// Minimal HTTP GET wrapper that reads a URL and returns the body.
// On Windows this uses the OS WinHTTP API (TLS 1.2/1.3, with timeouts so the
// call never hangs). Other platforms keep the harmless stub. The caller must
// run this from a worker thread since the request is synchronous/blocking.
std::string httpGet(const std::string &url, std::string &error)
{
#if defined(_WIN32)
    const std::wstring wurl = toWide(url);
    HINTERNET hSession = WinHttpOpen(L"MViewer-UpdateChecker/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        error = "UpdateChecker: WinHttpOpen failed";
        return {};
    }

    DWORD secure = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &secure, sizeof(secure));
    // resolve, connect, send, receive timeouts (ms)
    WinHttpSetTimeouts(hSession, 5000, 5000, 10000, 15000);

    wchar_t hostBuf[256] = {0};
    wchar_t pathBuf[2048] = {0};
    wchar_t extraBuf[2048] = {0};
    URL_COMPONENTSW parts = {sizeof(parts)};
    parts.dwSchemeLength = (DWORD)-1;
    parts.lpszHostName = hostBuf;
    parts.dwHostNameLength = sizeof(hostBuf) / sizeof(wchar_t);
    parts.lpszUrlPath = pathBuf;
    parts.dwUrlPathLength = sizeof(pathBuf) / sizeof(wchar_t);
    parts.lpszExtraInfo = extraBuf;
    parts.dwExtraInfoLength = sizeof(extraBuf) / sizeof(wchar_t);

    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &parts)) {
        WinHttpCloseHandle(hSession);
        error = "UpdateChecker: invalid URL";
        return {};
    }

    std::wstring host(hostBuf, parts.dwHostNameLength);
    std::wstring path(pathBuf, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength)
        path.append(extraBuf, parts.dwExtraInfoLength);
    if (path.empty())
        path = L"/";
    const bool https = (parts.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), parts.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        error = "UpdateChecker: connect failed";
        return {};
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, nullptr,
                                            https ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        error = "UpdateChecker: open request failed";
        return {};
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
        error = "UpdateChecker: request failed";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {};
    }

    DWORD status = 0, slen = sizeof(status);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            nullptr, &status, &slen, nullptr) &&
        status != 200) {
        error = "UpdateChecker: HTTP " + std::to_string(status);
    }

    std::string body;
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
        std::string buf(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, buf.data(), avail, &read) || read == 0)
            break;
        body.append(buf, 0, read);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return body;
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
