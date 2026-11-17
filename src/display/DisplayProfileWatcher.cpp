#include "display/DisplayProfileWatcher.h"

#if defined(Q_OS_WIN)
#include <QWinEventNotifier>
#include <utility>

DisplayProfileWatcher::DisplayProfileWatcher(HKEY root, QString path, std::function<void()> changed,
                                             QObject *parent)
    : QObject(parent), m_root(root), m_path(std::move(path)), m_changed(std::move(changed)),
      m_event(CreateEventW(nullptr, TRUE, FALSE, nullptr))
{
    if (!m_event)
        return;
    m_notifier = new QWinEventNotifier(m_event, this);
    m_notifier->setEnabled(false);
    connect(m_notifier, &QWinEventNotifier::activated, this, [this] { activated(); });
    m_notifier->setEnabled(arm());
}

DisplayProfileWatcher::~DisplayProfileWatcher()
{
    delete m_notifier;
    if (m_key)
        RegCloseKey(m_key);
    if (m_event)
        CloseHandle(m_event);
}

bool DisplayProfileWatcher::isActive() const
{
    return m_notifier && m_notifier->isEnabled();
}

bool DisplayProfileWatcher::arm()
{
    if (m_key)
    {
        RegCloseKey(m_key);
        m_key = nullptr;
    }
    ResetEvent(m_event);
    QString candidate = m_path;
    while (!candidate.isEmpty())
    {
        if (RegOpenKeyExW(m_root, reinterpret_cast<LPCWSTR>(candidate.utf16()), 0,
                          KEY_NOTIFY | KEY_WOW64_64KEY, &m_key) == ERROR_SUCCESS)
            break;
        const auto slash = candidate.lastIndexOf(QLatin1Char('\\'));
        candidate = slash < 0 ? QString() : candidate.left(slash);
    }
    if (!m_key)
        return false;
    return RegNotifyChangeKeyValue(m_key, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET,
                                   m_event, TRUE) == ERROR_SUCCESS;
}

void DisplayProfileWatcher::activated()
{
    m_notifier->setEnabled(false);
    // Rearm before invoking clients; subsequent association edits cannot be lost
    // while a viewer refreshes its context. A deleted key reopens its ancestor.
    m_notifier->setEnabled(arm());
    if (m_changed)
        m_changed();
}
#endif
