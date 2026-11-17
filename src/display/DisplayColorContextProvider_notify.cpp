#include "display/DisplayColorContextProvider.h"
#include "display/DisplayProfileWatcher.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QTimer>

#if defined(Q_OS_WIN)
#define NOMINMAX
#include <windows.h>
#endif

namespace
{
void queueProfileChange()
{
    // Native callbacks must not synchronously reenter QWidget / GetDC paths.
    static bool pending = false;
    if (pending || !QCoreApplication::instance())
        return;
    pending = true;
    QTimer::singleShot(0, QCoreApplication::instance(),
                       []
                       {
                           pending = false;
                           DisplayColorContextProvider::notifySystemColorSpaceChanged();
                       });
}

class ColorProfileNativeFilter : public QAbstractNativeEventFilter
{
  public:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override
    {
        Q_UNUSED(result);
#if defined(Q_OS_WIN)
        if (message && (eventType == QByteArrayLiteral("windows_generic_MSG") ||
                        eventType == QByteArrayLiteral("windows_dispatcher_MSG")))
        {
            const auto *msg = static_cast<const MSG *>(message);
            if (DisplayColorContextProvider::nativeMessageInvalidatesCache(msg->message, {}))
                queueProfileChange();
        }
#else
        Q_UNUSED(eventType);
        Q_UNUSED(message);
#endif
        return false;
    }
};

ColorProfileNativeFilter &nativeFilter()
{
    static ColorProfileNativeFilter filter;
    return filter;
}

QPointer<DisplayColorSpaceHub> &hubPointer()
{
    static QPointer<DisplayColorSpaceHub> hub;
    return hub;
}
} // namespace

DisplayColorSpaceHub::DisplayColorSpaceHub(QObject *parent) : QObject(parent)
{
    if (auto *app = QCoreApplication::instance())
        app->installNativeEventFilter(&nativeFilter());
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
    {
        connect(app, &QGuiApplication::screenRemoved, this,
                [](QScreen *) { queueProfileChange(); });
        connect(app, &QGuiApplication::screenAdded, this, [](QScreen *) { queueProfileChange(); });
    }
#if defined(Q_OS_WIN)
    // Microsoft WCS: monitor both per-user and system profile associations.
    // Watching existing ancestors also observes missing leaf creation/deletion.
    new DisplayProfileWatcher(
        HKEY_CURRENT_USER,
        QStringLiteral("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ICM\\ProfileAssociations"),
        queueProfileChange, this);
    new DisplayProfileWatcher(HKEY_LOCAL_MACHINE,
                              QStringLiteral("SYSTEM\\CurrentControlSet\\Control\\Class"),
                              queueProfileChange, this);
#endif
}

DisplayColorSpaceHub::~DisplayColorSpaceHub()
{
    if (auto *app = QCoreApplication::instance())
        app->removeNativeEventFilter(&nativeFilter());
}

DisplayColorSpaceHub *DisplayColorContextProvider::colorSpaceHub()
{
    auto *app = QCoreApplication::instance();
    if (!app)
        return nullptr;
    auto &hub = hubPointer();
    if (!hub)
        hub = new DisplayColorSpaceHub(app);
    return hub;
}

bool DisplayColorContextProvider::nativeMessageInvalidatesCache(unsigned message,
                                                                const QString &area)
{
    Q_UNUSED(area);
    // Profile association changes are registry events, NOT a guessed WM_* id.
    return message == 0x007E; // WM_DISPLAYCHANGE (also covers topology / HDR changes).
}
