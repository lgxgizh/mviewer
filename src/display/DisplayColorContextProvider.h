#pragma once

#include "core/image/DisplayColorContext.h"

#include <QObject>
#include <QString>
#include <QWindow>

#include <functional>
#include <string>
#include <vector>

// Emits when the OS reports a display color-space / ICM profile change so
// viewers can re-bind without polling or re-reading on every resize.
class DisplayColorSpaceHub : public QObject
{
    Q_OBJECT
  public:
    explicit DisplayColorSpaceHub(QObject *parent = nullptr);
    ~DisplayColorSpaceHub() override;

  signals:
    void colorSpaceChanged();
};

// UI-layer bridge from a QWindow to the active per-display ICC profile.  The
// Qt-free DisplayColorContext value crosses into core; Win32/Qt handles stay
// in this header/implementation boundary.  Loaded profiles are cached per
// screen identity until CMS, screen identity, screen add/remove, or a system
// color-space notification invalidates them.  A null window's sRGB fallback
// is not stored as a monitor profile.
class DisplayColorContextProvider
{
  public:
    using ProfileLoader = std::function<std::vector<uint8_t>(const QWindow *window)>;

    static mviewer::core::DisplayColorContext forWindow(const QWindow *window);
    // True when `current` is already the cached target for `window`.  The check
    // compares fingerprint and generation only, so a resize does not copy the
    // ICC byte buffer.
    static bool targetUnchanged(const QWindow *window,
                                const mviewer::core::DisplayColorContext &current);
    static bool isColorManagementEnabled();
    static void setColorManagementEnabled(bool enabled);

    static DisplayColorSpaceHub *colorSpaceHub();
    static void notifySystemColorSpaceChanged();
    static bool nativeMessageInvalidatesCache(unsigned message, const QString &area);

    static void setProfileLoaderForTest(ProfileLoader loader);
    static int profileLoadCountForTest();
    static void setScreenIdentityForTest(std::string identity);
    static void resetForTest();
    static int windowStateCountForTest();
};
