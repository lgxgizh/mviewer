#pragma once

#include <QDir>
#include <QHash>
#include <QString>
#include <QStringList>

#include <utility>

// Path -> first row lookup for browse lists. The previous linear scans ran
// QDir::cleanPath on BOTH sides for every row of every lookup (each arrow key
// in a 50k-image folder cost tens of milliseconds). Paths are normalized once
// per list change here and looked up through a hash; equality semantics are
// unchanged (case-insensitive on Windows).
class BrowsePathIndex
{
  public:
    enum class Separators
    {
        Native, // QDir::fromNativeSeparators ('\' only converted on Windows)
        Always  // '\' always treated as a separator
    };

    explicit BrowsePathIndex(Separators separators) : m_separators(separators)
    {
    }

    QString key(QString path) const
    {
        if (m_separators == Separators::Always)
            path.replace(QLatin1Char('\\'), QLatin1Char('/'));
        else
            path = QDir::fromNativeSeparators(path);
        // cleanPath allocates; skip it when it would be the identity (no
        // doubled separators, no "." / ".." segments, no trailing slash).
        const bool needsClean = path.contains(QLatin1String("//")) ||
                                path.contains(QLatin1String("/.")) ||
                                path.startsWith(QLatin1Char('.')) ||
                                (path.size() > 1 && path.endsWith(QLatin1Char('/')));
        if (needsClean)
            path = QDir::cleanPath(path);
#ifdef Q_OS_WIN
        path = path.toCaseFolded();
#endif
        return path;
    }

    // Invalidate after the list changed; the hash is rebuilt on next lookup.
    void invalidate()
    {
        m_valid = false;
        m_lookupsSinceChange = 0;
        m_rows.clear();
    }

    int indexOf(const QStringList &paths, const QString &path) const
    {
        if (paths.isEmpty())
            return -1;
        // First lookup after a change (e.g. one per progressive-scan batch):
        // stop at the first match without allocating. A second lookup on the
        // same list (arrow-key navigation) builds the hash once.
        if (!m_valid && m_lookupsSinceChange++ == 0)
            return scan(paths, key(path));
        if (!m_valid)
        {
            m_rows.clear();
            m_rows.reserve(paths.size());
            for (int i = 0; i < paths.size(); ++i)
            {
                QString k = key(paths.at(i));
                if (!m_rows.contains(k)) // duplicates resolve to the first row
                    m_rows.insert(std::move(k), i);
            }
            m_valid = true;
        }
        return m_rows.value(key(path), -1);
    }

  private:
    // A path is already in key form when it has no separator to convert and
    // nothing cleanPath would change; such rows are compared without copying.
    bool alreadyClean(const QString &path) const
    {
#ifdef Q_OS_WIN
        const bool convert = true;
#else
        const bool convert = m_separators == Separators::Always;
#endif
        return !(convert && path.contains(QLatin1Char('\\'))) &&
               !path.contains(QLatin1String("//")) && !path.contains(QLatin1String("/.")) &&
               !path.startsWith(QLatin1Char('.')) &&
               !(path.size() > 1 && path.endsWith(QLatin1Char('/')));
    }

    int scan(const QStringList &paths, const QString &wanted) const
    {
        for (int i = 0; i < paths.size(); ++i)
        {
            const QString &row = paths.at(i);
            if (alreadyClean(row))
            {
#ifdef Q_OS_WIN
                if (row.compare(wanted, Qt::CaseInsensitive) == 0)
                    return i;
#else
                if (row == wanted)
                    return i;
#endif
            }
            else if (key(row) == wanted)
            {
                return i;
            }
        }
        return -1;
    }

    Separators m_separators;
    mutable int m_lookupsSinceChange = 0;
    mutable QHash<QString, int> m_rows;
    mutable bool m_valid = false;
};
