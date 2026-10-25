#pragma once

#include <QDir>
#include <QString>
#include <QStringList>

// Directory back/forward stack for the right browse pane. Cap matches the
// left pane (50). Paths are slash-normalized so "C:/a" and "C:\\a" are one entry.
class SecondaryBrowseHistory
{
  public:
    static constexpr int kCap = 50;

    void push(const QString &dir)
    {
        const QString normalized = normalize(dir);
        if (normalized.isEmpty())
            return;
        if (m_index >= 0 && m_index < m_entries.size() && same(m_entries.at(m_index), normalized))
            return;
        while (m_index >= 0 && m_index + 1 < m_entries.size())
            m_entries.removeLast();
        if (!m_entries.isEmpty() && same(m_entries.last(), normalized))
            return;
        m_entries.append(normalized);
        m_index = m_entries.size() - 1;
        while (m_entries.size() > kCap)
        {
            m_entries.removeFirst();
            --m_index;
        }
    }

    QString back()
    {
        if (!canBack())
            return {};
        --m_index;
        return m_entries.at(m_index);
    }

    QString forward()
    {
        if (!canForward())
            return {};
        ++m_index;
        return m_entries.at(m_index);
    }

    bool canBack() const
    {
        return m_index > 0;
    }
    bool canForward() const
    {
        return m_index >= 0 && m_index + 1 < m_entries.size();
    }
    QString current() const
    {
        if (m_index < 0 || m_index >= m_entries.size())
            return {};
        return m_entries.at(m_index);
    }
    QStringList entries() const
    {
        return m_entries;
    }
    int index() const
    {
        return m_index;
    }

    void restore(const QStringList &entries, int index)
    {
        m_entries.clear();
        for (const QString &entry : entries)
        {
            const QString normalized = normalize(entry);
            if (normalized.isEmpty())
                continue;
            if (!m_entries.isEmpty() && same(m_entries.last(), normalized))
                continue;
            m_entries.append(normalized);
        }
        const int overflow = m_entries.size() > kCap ? m_entries.size() - kCap : 0;
        while (m_entries.size() > kCap)
            m_entries.removeFirst();
        if (m_entries.isEmpty())
        {
            m_index = -1;
            return;
        }
        m_index = index - overflow;
        if (m_index < 0)
            m_index = 0;
        if (m_index >= m_entries.size())
            m_index = m_entries.size() - 1;
    }

  private:
    static QString normalize(const QString &dir)
    {
        if (dir.isEmpty())
            return {};
        return QDir::cleanPath(QDir::fromNativeSeparators(dir));
    }

    static bool same(const QString &left, const QString &right)
    {
#ifdef Q_OS_WIN
        return left.compare(right, Qt::CaseInsensitive) == 0;
#else
        return left == right;
#endif
    }

    QStringList m_entries;
    int m_index = -1;
};
