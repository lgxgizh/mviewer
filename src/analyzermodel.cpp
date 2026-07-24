#include "analyzermodel.h"

AnalyzerModel::AnalyzerModel(QObject *parent) : QObject(parent)
{
}

void AnalyzerModel::setCurrentAnalyzer(const QString &id)
{
    if (m_currentAnalyzer == id)
        return;
    m_currentAnalyzer = id;
    emit currentAnalyzerChanged(m_currentAnalyzer);
}

void AnalyzerModel::setResult(const QString &imagePath, const QString &text)
{
    if (imagePath.isEmpty())
        return;
    // Cap growth across long browse sessions (same hygiene as the old map).
    if (!m_results.contains(imagePath) && m_results.size() >= kMaxResults)
    {
        // Prefer dropping the oldest history entry that is not pinned; fall back
        // to any non-pinned key. Never evict pinned results. If everything is
        // pinned, refuse the insert so the map cannot grow unbounded.
        QString dropKey;
        for (const QString &h : m_history)
        {
            if (m_results.contains(h) && !m_pinned.contains(h))
            {
                dropKey = h;
                break;
            }
        }
        if (dropKey.isEmpty())
        {
            for (auto it = m_results.begin(); it != m_results.end(); ++it)
            {
                if (!m_pinned.contains(it.key()))
                {
                    dropKey = it.key();
                    break;
                }
            }
        }
        if (dropKey.isEmpty())
            return; // all pinned at capacity — keep existing results
        m_results.remove(dropKey);
        emit resultChanged(dropKey, QString());
    }
    if (m_results.value(imagePath) == text)
        return;
    m_results.insert(imagePath, text);
    emit resultChanged(imagePath, text);
    pushHistory(imagePath);
}

void AnalyzerModel::clearResult(const QString &imagePath)
{
    if (!m_results.remove(imagePath))
        return;
    emit resultChanged(imagePath, QString());
}

void AnalyzerModel::clearAllResults()
{
    if (m_results.isEmpty())
        return;
    const QStringList keys = m_results.keys();
    m_results.clear();
    for (const QString &k : keys)
        emit resultChanged(k, QString());
}

void AnalyzerModel::pinResult(const QString &imagePath)
{
    if (imagePath.isEmpty() || m_pinned.contains(imagePath))
        return;
    m_pinned.append(imagePath);
    emit pinnedChanged(m_pinned);
}

void AnalyzerModel::unpinResult(const QString &imagePath)
{
    if (!m_pinned.removeOne(imagePath))
        return;
    emit pinnedChanged(m_pinned);
}

void AnalyzerModel::pushHistory(const QString &imagePath)
{
    if (imagePath.isEmpty())
        return;
    m_history.removeAll(imagePath);
    m_history.prepend(imagePath);
    while (m_history.size() > kMaxHistory)
        m_history.removeLast();
    emit historyChanged(m_history);
}
