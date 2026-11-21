#include "check.h"
#include "compareworkspace.h"
#include "display/DisplayColorContextProvider.h"
#include "imageviewer.h"

#include "display/DisplayProfileWatcher.h"
#include <QApplication>
#include <QColorSpace>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QResizeEvent>
#include <QSettings>
#include <QTemporaryDir>
#include <QWindow>

#include <string>
#include <vector>

namespace
{

std::vector<uint8_t> profileA()
{
    const QByteArray bytes = QColorSpace(QColorSpace::SRgb).iccProfile();
    return {reinterpret_cast<const uint8_t *>(bytes.constData()),
            reinterpret_cast<const uint8_t *>(bytes.constData()) + bytes.size()};
}

std::vector<uint8_t> profileB()
{
    return {9, 8, 7, 6, 5, 4};
}

void installLoader(std::vector<uint8_t> bytes)
{
    DisplayColorContextProvider::setProfileLoaderForTest([bytes](const QWindow *)
                                                         { return bytes; });
}

void realize(QWidget *widget)
{
    widget->resize(400, 300);
    widget->show();
    widget->winId();
    QApplication::processEvents();
}

void sendResize(QWidget *widget, int width, int height)
{
    const QSize previous = widget->size();
    widget->resize(width, height);
    QResizeEvent event(QSize(width, height), previous);
    QCoreApplication::sendEvent(widget, &event);
    QApplication::processEvents();
}

void testRepeatedForWindowLoadsOnce()
{
    DisplayColorContextProvider::resetForTest();
    DisplayColorContextProvider::setScreenIdentityForTest("screen-a");
    installLoader(profileA());

    const auto first = DisplayColorContextProvider::forWindow(nullptr);
    const auto second = DisplayColorContextProvider::forWindow(nullptr);
    const auto third = DisplayColorContextProvider::forWindow(nullptr);

    MVIEWER_CHECK(DisplayColorContextProvider::profileLoadCountForTest() == 1,
                  "repeated forWindow loads the monitor profile once");
    MVIEWER_CHECK(first.cacheKey() == second.cacheKey() && second.cacheKey() == third.cacheKey(),
                  "cached context keeps fingerprint and generation");
    MVIEWER_CHECK(first.hasProfile() && first.generation == 1,
                  "first managed load is generation 1");
}

void testScreenIdentityReloads()
{
    DisplayColorContextProvider::resetForTest();
    DisplayColorContextProvider::setScreenIdentityForTest("screen-a");
    installLoader(profileA());
    const auto first = DisplayColorContextProvider::forWindow(nullptr);

    DisplayColorContextProvider::setProfileLoaderForTest([](const QWindow *)
                                                         { return profileB(); });
    DisplayColorContextProvider::setScreenIdentityForTest("screen-b");
    const auto moved = DisplayColorContextProvider::forWindow(nullptr);

    MVIEWER_CHECK(DisplayColorContextProvider::profileLoadCountForTest() == 2,
                  "a different screen identity reloads the profile");
    MVIEWER_CHECK(moved.cacheKey() != first.cacheKey(), "screen change updates cache identity");
    MVIEWER_CHECK(moved.generation == first.generation + 1,
                  "fingerprint change bumps per-window generation");
}

void testProfileChangeNotificationReloads()
{
    DisplayColorContextProvider::resetForTest();
    DisplayColorContextProvider::setScreenIdentityForTest("screen-a");
    std::vector<uint8_t> current = profileA();
    DisplayColorContextProvider::setProfileLoaderForTest([&current](const QWindow *)
                                                         { return current; });

    const auto first = DisplayColorContextProvider::forWindow(nullptr);
    current = profileB();
    DisplayColorContextProvider::notifySystemColorSpaceChanged();
    const auto updated = DisplayColorContextProvider::forWindow(nullptr);

    MVIEWER_CHECK(DisplayColorContextProvider::profileLoadCountForTest() == 2,
                  "system color-space notification reloads the same screen");
    MVIEWER_CHECK(updated.cacheKey() != first.cacheKey(),
                  "same-screen profile change updates cache identity");
    MVIEWER_CHECK(updated.generation == first.generation + 1,
                  "same-screen profile change preserves generation bump");
}

void testCmsDisabledDoesNotLoad()
{
    DisplayColorContextProvider::resetForTest();
    DisplayColorContextProvider::setScreenIdentityForTest("screen-a");
    installLoader(profileA());
    DisplayColorContextProvider::setColorManagementEnabled(false);

    const auto first = DisplayColorContextProvider::forWindow(nullptr);
    const auto second = DisplayColorContextProvider::forWindow(nullptr);

    MVIEWER_CHECK(DisplayColorContextProvider::profileLoadCountForTest() == 0,
                  "CMS disabled never invokes the profile loader");
    MVIEWER_CHECK(!first.colorManagementEnabled && first.fingerprint == "raw",
                  "CMS disabled returns a raw context");
    MVIEWER_CHECK(first.cacheKey() == second.cacheKey(), "raw context is stable across calls");

    DisplayColorContextProvider::setColorManagementEnabled(true);
    const auto enabled = DisplayColorContextProvider::forWindow(nullptr);
    MVIEWER_CHECK(DisplayColorContextProvider::profileLoadCountForTest() == 1,
                  "re-enabling CMS loads the profile once");
    MVIEWER_CHECK(enabled.hasProfile() && enabled.generation == first.generation + 1,
                  "CMS on after off bumps generation");
}

void testNativeMessageClassification()
{
    MVIEWER_CHECK(DisplayColorContextProvider::nativeMessageInvalidatesCache(0x007E, {}),
                  "WM_DISPLAYCHANGE invalidates the cached profile");
    MVIEWER_CHECK(!DisplayColorContextProvider::nativeMessageInvalidatesCache(0x0361, {}),
                  "undocumented message is not an ICC notification");
}

void testNullWindowDoesNotPoisonCache()
{
    DisplayColorContextProvider::resetForTest();
    const auto fallback = DisplayColorContextProvider::forWindow(nullptr);
    MVIEWER_CHECK(!fallback.hasProfile(), "null HWND is sRGB fallback");
    MVIEWER_CHECK(DisplayColorContextProvider::profileLoadCountForTest() == 0,
                  "null HWND does not load or cache a monitor profile");
    installLoader(profileA());
    const auto managed = DisplayColorContextProvider::forWindow(nullptr);
    MVIEWER_CHECK(managed.hasProfile(), "null fallback did not poison monitor cache");
}

void testInvalidationDuringLoad()
{
    DisplayColorContextProvider::resetForTest();
    int calls = 0;
    DisplayColorContextProvider::setProfileLoaderForTest(
        [&calls](const QWindow *)
        {
            if (++calls == 1)
            {
                DisplayColorContextProvider::notifySystemColorSpaceChanged();
                return profileA();
            }
            return profileB();
        });
    const auto loaded = DisplayColorContextProvider::forWindow(nullptr);
    const auto cached = DisplayColorContextProvider::forWindow(nullptr);
    MVIEWER_CHECK(calls == 2, "invalidation during unlocked load retries once");
    MVIEWER_CHECK(loaded.fingerprint ==
                      mviewer::core::DisplayColorContext::fromIccProfile(profileB()).fingerprint,
                  "obsolete ICC cannot overwrite the invalidation");
    MVIEWER_CHECK(cached.cacheKey() == loaded.cacheKey(), "fresh result was cached");
    installLoader(profileA()); // drop reference to stack-local counter
}

#if defined(Q_OS_WIN)
bool waitForCount(const int &count, int expected)
{
    QElapsedTimer timer;
    timer.start();
    while (count < expected && timer.elapsed() < 5000)
        QApplication::processEvents();
    return count >= expected;
}

void testRegistryWatcher()
{
    // Disposable HKCU test subtree only. Never edit real profile associations.
    const QString base =
        QStringLiteral("SOFTWARE\\MViewerTestICC_%1").arg(QCoreApplication::applicationPid());
    HKEY key = nullptr;
    const auto wide = reinterpret_cast<LPCWSTR>(base.utf16());
    MVIEWER_CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, wide, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr,
                                  &key, nullptr) == ERROR_SUCCESS,
                  "create disposable registry root");
    if (!key)
        return;
    int changes = 0;
    {
        DisplayProfileWatcher watcher(
            HKEY_CURRENT_USER, base + QStringLiteral("\\Missing"), [&changes] { ++changes; },
            nullptr);
        MVIEWER_CHECK(watcher.isActive(), "missing leaf watches existing ancestor");
        DWORD value = 1;
        RegSetValueExW(key, L"test", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value),
                       sizeof(value));
        MVIEWER_CHECK(waitForCount(changes, 1), "registry mutation fires asynchronously");
        const int previous = changes;
        ++value;
        RegSetValueExW(key, L"test", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value),
                       sizeof(value));
        MVIEWER_CHECK(waitForCount(changes, previous + 1), "watcher rearms after notification");
    }
    RegCloseKey(key);
    RegDeleteTreeW(HKEY_CURRENT_USER, wide);
}
#endif

void testWindowStateLifetime()
{
    DisplayColorContextProvider::resetForTest();
    installLoader(profileA());
    {
        QWindow window;
        DisplayColorContextProvider::forWindow(&window);
        MVIEWER_CHECK(DisplayColorContextProvider::windowStateCountForTest() == 1,
                      "profile generation tracked for live window");
    }
    MVIEWER_CHECK(DisplayColorContextProvider::windowStateCountForTest() == 0,
                  "destroyed window releases generation state");
}

void testImageViewerResizeUsesCache()
{
    DisplayColorContextProvider::resetForTest();
    installLoader(profileA());

    ImageViewer viewer;
    realize(&viewer);
    sendResize(&viewer, 420, 320);
    const int afterBind = DisplayColorContextProvider::profileLoadCountForTest();
    MVIEWER_CHECK(afterBind >= 1, "ImageViewer bind loads the monitor profile");

    sendResize(&viewer, 640, 480);
    sendResize(&viewer, 800, 600);
    MVIEWER_CHECK(DisplayColorContextProvider::profileLoadCountForTest() == afterBind,
                  "ImageViewer resize reuses the cached profile");

    const std::string before = viewer.displayColorContext().cacheKey();
    DisplayColorContextProvider::setProfileLoaderForTest([](const QWindow *)
                                                         { return profileB(); });
    DisplayColorContextProvider::notifySystemColorSpaceChanged();
    QApplication::processEvents();
    MVIEWER_CHECK(DisplayColorContextProvider::profileLoadCountForTest() == afterBind + 1,
                  "ImageViewer reloads after a system profile change");
    MVIEWER_CHECK(viewer.displayColorContext().cacheKey() != before,
                  "ImageViewer applies the new display color context");

    DisplayColorContextProvider::setColorManagementEnabled(false);
    QApplication::processEvents();
    MVIEWER_CHECK(DisplayColorContextProvider::profileLoadCountForTest() == afterBind + 1,
                  "ImageViewer CMS off does not load a profile");
    MVIEWER_CHECK(!viewer.displayColorContext().colorManagementEnabled,
                  "ImageViewer CMS off is raw");
}

void testCompareWorkspaceResizeUsesCache()
{
    DisplayColorContextProvider::resetForTest();
    installLoader(profileA());

    CompareWorkspace workspace;
    realize(&workspace);
    sendResize(&workspace, 900, 600);
    const int afterBind = DisplayColorContextProvider::profileLoadCountForTest();
    MVIEWER_CHECK(afterBind >= 1, "CompareWorkspace bind loads the monitor profile");

    sendResize(&workspace, 1000, 700);
    sendResize(&workspace, 1100, 720);
    MVIEWER_CHECK(DisplayColorContextProvider::profileLoadCountForTest() == afterBind,
                  "CompareWorkspace resize reuses the cached profile");

    DisplayColorContextProvider::notifySystemColorSpaceChanged();
    QApplication::processEvents();
    MVIEWER_CHECK(DisplayColorContextProvider::profileLoadCountForTest() == afterBind + 1,
                  "CompareWorkspace reloads after a system profile change");
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir settings;
    QCoreApplication::setOrganizationName(QStringLiteral("MViewerTests"));
    QCoreApplication::setApplicationName(QStringLiteral("ICCProfileCache"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    testRepeatedForWindowLoadsOnce();
    testScreenIdentityReloads();
    testProfileChangeNotificationReloads();
    testCmsDisabledDoesNotLoad();
    testNativeMessageClassification();
    testNullWindowDoesNotPoisonCache();
    testInvalidationDuringLoad();
    testWindowStateLifetime();
#if defined(Q_OS_WIN)
    testRegistryWatcher();
#endif
    testImageViewerResizeUsesCache();
    testCompareWorkspaceResizeUsesCache();
    return mviewer::test::failures() == 0 ? 0 : 1;
}
