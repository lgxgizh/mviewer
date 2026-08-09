#include "core/update/UpdateChecker.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>
#include <QUrl>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

// Parse a dotted numeric release version such as "v1.2.3" into {1, 2, 3}.
std::optional<std::vector<uint64_t>> parseSemver(const std::string &tag)
{
    if (tag.empty())
        return std::nullopt;

    std::vector<uint64_t> parts;
    size_t i = (tag[0] == 'v' || tag[0] == 'V') ? 1 : 0;
    if (i == tag.size())
        return std::nullopt;

    while (i < tag.size())
    {
        if (!std::isdigit(static_cast<unsigned char>(tag[i])))
            return std::nullopt;

        uint64_t value = 0;
        do
        {
            const uint64_t digit = static_cast<uint64_t>(tag[i] - '0');
            if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10)
                return std::nullopt;
            value = value * 10 + digit;
            ++i;
        } while (i < tag.size() && std::isdigit(static_cast<unsigned char>(tag[i])));
        parts.push_back(value);

        if (i == tag.size())
            break;
        if (tag[i] != '.' || ++i == tag.size())
            return std::nullopt;
    }

    if (parts.size() < 2)
        return std::nullopt;
    return parts;
}

// Return -1, 0, or 1. Missing trailing components compare as zero.
std::optional<int> compareSemver(const std::string &lhsTag, const std::string &rhsTag)
{
    const auto lhs = parseSemver(lhsTag);
    const auto rhs = parseSemver(rhsTag);
    if (!lhs || !rhs)
        return std::nullopt;

    const size_t componentCount = std::max(lhs->size(), rhs->size());
    for (size_t i = 0; i < componentCount; ++i)
    {
        const uint64_t left = i < lhs->size() ? (*lhs)[i] : 0;
        const uint64_t right = i < rhs->size() ? (*rhs)[i] : 0;
        if (left != right)
            return left > right ? 1 : -1;
    }
    return 0;
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
    HINTERNET hSession =
        WinHttpOpen(L"MViewer-UpdateChecker/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession)
    {
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

    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &parts))
    {
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
    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        error = "UpdateChecker: connect failed";
        return {};
    }

    HINTERNET hRequest =
        WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, nullptr,
                           https ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        error = "UpdateChecker: open request failed";
        return {};
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr))
    {
        error = "UpdateChecker: request failed";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {};
    }

    DWORD status = 0, slen = sizeof(status);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            nullptr, &status, &slen, nullptr) &&
        status != 200)
    {
        error = "UpdateChecker: HTTP " + std::to_string(status);
    }

    std::string body;
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0)
    {
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

// Parse the release fields with Qt JSON so whitespace, ordering, and escapes
// follow JSON rules without leaking Qt types into the public core header.
struct ReleasePayload
{
    std::string tagName;
    std::string htmlUrl;
};

std::optional<ReleasePayload> parseReleasePayload(const std::string &json, std::string &error)
{
    QJsonParseError parseError;
    const QByteArray bytes(json.data(), static_cast<qsizetype>(json.size()));
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        error = "UpdateChecker: invalid release JSON";
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    const auto tagValue = object.value(QStringLiteral("tag_name"));
    if (!tagValue.isString() || tagValue.toString().isEmpty())
    {
        error = "UpdateChecker: could not parse tag_name from response";
        return std::nullopt;
    }

    ReleasePayload payload;
    payload.tagName = tagValue.toString().toStdString();
    const auto htmlValue = object.value(QStringLiteral("html_url"));
    if (htmlValue.isString())
        payload.htmlUrl = htmlValue.toString().toStdString();
    return payload;
}

bool isGitHubComponent(const std::string &component)
{
    if (component.empty() || component == "." || component == "..")
        return false;
    return std::all_of(component.begin(), component.end(),
                       [](unsigned char c)
                       {
                           return std::isalnum(c) || c == '-' || c == '_' || c == '.';
                       });
}

std::string deriveGitHubReleaseUrl(const std::string &apiUrl, const std::string &tag)
{
    constexpr std::string_view prefix = "https://api.github.com/repos/";
    constexpr std::string_view suffix = "/releases/latest";
    if (!apiUrl.starts_with(prefix) || !apiUrl.ends_with(suffix) ||
        apiUrl.size() <= prefix.size() + suffix.size())
    {
        return {};
    }

    const std::string repo =
        apiUrl.substr(prefix.size(), apiUrl.size() - prefix.size() - suffix.size());
    const size_t slash = repo.find('/');
    if (slash == std::string::npos || repo.find('/', slash + 1) != std::string::npos ||
        !isGitHubComponent(repo.substr(0, slash)) || !isGitHubComponent(repo.substr(slash + 1)))
    {
        return {};
    }
    return "https://github.com/" + repo + "/releases/tag/" + tag;
}

bool isValidHttpsUrl(const std::string &candidate)
{
    if (candidate.empty())
        return false;

    const QUrl url(QString::fromStdString(candidate), QUrl::StrictMode);
    const QString scheme = url.scheme();
    return url.isValid() && !url.isRelative() && !url.host().isEmpty() &&
           scheme.compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0;
}

std::string validatedReleaseUrl(const std::string &candidate)
{
    return isValidHttpsUrl(candidate) ? candidate : std::string{};
}

} // namespace

UpdateChecker::UpdateChecker(std::string currentVersion, FetchFn fetchFn)
    : m_currentVersion(std::move(currentVersion)),
      m_fetchFn(fetchFn ? std::move(fetchFn) : FetchFn(httpGet))
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

    if (!isValidHttpsUrl(updateJsonUrl))
    {
        info.error = "UpdateChecker: update endpoint must be an absolute HTTPS URL";
        if (cb)
            cb(info);
        return;
    }

    std::string error;
    std::string body;
    try
    {
        body = m_fetchFn(updateJsonUrl, error);
    }
    catch (const std::exception &exception)
    {
        error = "UpdateChecker: fetch failed: " + std::string(exception.what());
    }
    catch (...)
    {
        error = "UpdateChecker: fetch failed with an unknown exception";
    }
    if (!error.empty())
    {
        info.error = error;
        if (cb)
            cb(info);
        return;
    }

    const auto payload = parseReleasePayload(body, error);
    if (!payload)
    {
        info.error = error;
        if (cb)
            cb(info);
        return;
    }

    info.latestVersion = payload->tagName;
    const auto versionOrder = compareSemver(payload->tagName, m_currentVersion);
    if (!versionOrder)
    {
        info.error = "UpdateChecker: invalid dotted numeric release or current version";
        if (cb)
            cb(info);
        return;
    }
    info.hasUpdate = *versionOrder > 0;
    info.releaseUrl = validatedReleaseUrl(payload->htmlUrl);
    if (info.releaseUrl.empty())
        info.releaseUrl = deriveGitHubReleaseUrl(updateJsonUrl, payload->tagName);

    if (cb)
        cb(info);
}

} // namespace mviewer::core
