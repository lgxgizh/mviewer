#pragma once
#include <QtGlobal>

#if defined(Q_OS_WIN)
#include <QObject>
#include <QString>
#include <functional>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

class QWinEventNotifier;

// Event-driven WCS registry watcher. No polling, extra thread or profile I/O.
// A missing target is watched through its nearest existing ancestor.
class DisplayProfileWatcher final : public QObject
{
  public:
    DisplayProfileWatcher(HKEY root, QString path, std::function<void()> changed, QObject *parent);
    ~DisplayProfileWatcher() override;
    bool isActive() const;

  private:
    bool arm();
    void activated();
    HKEY m_root;
    QString m_path;
    std::function<void()> m_changed;
    HKEY m_key = nullptr;
    HANDLE m_event = nullptr;
    QWinEventNotifier *m_notifier = nullptr;
};
#endif
