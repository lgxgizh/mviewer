# UpdateChecker Specification

## Purpose

`mviewer::core::UpdateChecker` performs a synchronous metadata check for a newer release. It does
not download or install software. Callers that require asynchronous behavior run it on a worker
thread and marshal the `UpdateInfo` result to the UI.

The public header is Qt-free. The implementation may use Qt internally to parse JSON.

## Construction and fetching

```cpp
using FetchFn =
    std::function<std::string(const std::string &url, std::string &error)>;

explicit UpdateChecker(std::string currentVersion, FetchFn fetchFn = {});
```

When `fetchFn` is empty, the checker uses its platform HTTP implementation (WinHTTP on Windows).
Tests and embedding applications may inject a synchronous fetcher. A fetcher returns the response
body and reports transport or HTTP failure through `error`.

`checkGitHub("owner/repo", callback)` requests exactly:

```text
https://api.github.com/repos/owner/repo/releases/latest
```

`checkUrl(url, callback)` accepts only a valid absolute HTTPS endpoint. Invalid, HTTP, file, and
relative endpoints are rejected before the fetcher is called. A valid URL is passed to the
configured fetcher unchanged. Both methods invoke a non-empty callback once before returning,
including for endpoint, fetch, JSON, and version errors. Exceptions from an injected fetcher are
converted to an error result.

## Release response

The response must be a JSON object with a non-empty string `tag_name`. An optional string
`html_url` supplies the browser-facing release page. Standard JSON whitespace, field ordering, and
string escapes are supported.

Malformed JSON, a missing/non-string `tag_name`, or an invalid dotted numeric release version
produces an `UpdateInfo` with `hasUpdate == false` and a non-empty `error`.

## Result contract

- `currentVersion` always equals the version passed to the constructor.
- After valid JSON is parsed, `latestVersion` equals its `tag_name`, including when dotted-version
  validation subsequently reports an error.
- An endpoint, fetch, JSON, or version error returns `hasUpdate == false` and a non-empty `error`.
- A non-empty callback is invoked exactly once before `checkGitHub` or `checkUrl` returns.

## Version comparison

Accepted versions contain at least two dot-separated unsigned decimal components and may start
with `v` or `V`, for example `1.2`, `v1.2.3`, or `1.2.3.4`. Other characters, empty components, and
components that overflow `uint64_t` are invalid and never throw an exception.

Missing trailing components compare as zero, so `1.2`, `1.2.0`, and `1.2.0.0` are equal. An update
is available only when the release version is strictly greater than the current version.

## Release URL policy

`UpdateInfo::releaseUrl` uses the response `html_url` only when it is a valid absolute `https://`
URL with a host. HTTP, file, JavaScript, relative, and malformed URLs are rejected. If the field is
absent or rejected, a URL is derived only for the exact canonical GitHub endpoint form:

```text
https://api.github.com/repos/{owner}/{repo}/releases/latest
```

Both path components must be non-empty and contain only letters, digits, `.`, `_`, or `-`. Custom,
lookalike, malformed, query-bearing, or fragment-bearing endpoints without `html_url` leave
`releaseUrl` empty.
