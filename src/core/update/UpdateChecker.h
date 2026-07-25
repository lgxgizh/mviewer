#pragma once

#include <functional>
#include <string>

namespace mviewer::core
{

/// P2 #⑩: Lightweight auto-update checker.
///
/// Checks the latest GitHub release tag (or a custom URL) against the current
/// version.  Does NOT download or install — it only reports availability.
/// The UI layer decides whether to show a notification.
struct UpdateInfo
{
    bool hasUpdate = false;
    std::string latestVersion;   // e.g. "v1.5.0"
    std::string currentVersion;  // e.g. "v1.4.0"
    std::string releaseUrl;      // browser link to the release page
    std::string error;
};

/// Callback signature: void(UpdateInfo).
using UpdateCallback = std::function<void(const UpdateInfo &)>;

class UpdateChecker
{
  public:
    /// Construct with the current app version string (semver or tag).
    explicit UpdateChecker(std::string currentVersion);

    /// Query GitHub Releases for the latest version asynchronously.
    /// @param repo  GitHub owner/repo, e.g. "example/mviewer".
    /// @param cb    Called on the calling thread after the HTTP round-trip
    ///              completes (blocking; call from a worker thread).
    void checkGitHub(const std::string &repo, UpdateCallback cb);

    /// Query an arbitrary JSON endpoint that returns {"tag_name":"v1.2.3"}.
    void checkUrl(const std::string &updateJsonUrl, UpdateCallback cb);

  private:
    std::string m_currentVersion;
};

} // namespace mviewer::core
