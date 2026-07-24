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
        // Drop the oldest non-pinned entry (QMap is ordered by key; walk and
        // remove the first non-pinned). Pinned results are never evicted here.
        for (auto it = m_results.begin(); it != m_results.end(); ++it)
        {
            if (!m_pinned.contains(it.key()))
            {
                const QString dropped = it.key();
                m_results.erase(it);
                emit resultChanged(dropped, QString());
                break;
            }
        }
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
