#include "analyzermodel.h"

#include "runtime_storage.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

AnalyzerModel::AnalyzerModel(QObject *parent) : QObject(parent)
{
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    connect(m_saveTimer, &QTimer::timeout, this, &AnalyzerModel::save);
    connect(this, &AnalyzerModel::resultChanged, this, &AnalyzerModel::scheduleSave);
    connect(this, &AnalyzerModel::historyChanged, this, &AnalyzerModel::scheduleSave);
    connect(this, &AnalyzerModel::pinnedChanged, this, &AnalyzerModel::scheduleSave);
    connect(this, &AnalyzerModel::currentAnalyzerChanged, this, &AnalyzerModel::scheduleSave);
    load();
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
    // Legacy callers do not know the producer id. Clear it rather than
    // retaining an identity from a previous result, which could mislabel a
    // newly-published text after the user switches analyzers.
    setResult(imagePath, text, QString());
}

void AnalyzerModel::setResult(const QString &imagePath, const QString &text,
                              const QString &analyzerId)
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
        m_resultAnalyzers.remove(dropKey);
        emit resultChanged(dropKey, QString());
    }
    if (m_results.value(imagePath) == text && m_resultAnalyzers.value(imagePath) == analyzerId)
        return;
    m_results.insert(imagePath, text);
    m_resultAnalyzers.insert(imagePath, analyzerId);
    emit resultChanged(imagePath, text);
    pushHistory(imagePath);
}

void AnalyzerModel::clearResult(const QString &imagePath)
{
    if (!m_results.remove(imagePath))
        return;
    m_resultAnalyzers.remove(imagePath);
    emit resultChanged(imagePath, QString());
}

void AnalyzerModel::clearAllResults()
{
    if (m_results.isEmpty())
        return;
    const QStringList keys = m_results.keys();
    m_results.clear();
    m_resultAnalyzers.clear();
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

QString AnalyzerModel::storagePath() const
{
    return mviewer::runtime::filePath(QStandardPaths::AppDataLocation,
                                      QStringLiteral("analysis_history.json"));
}

void AnalyzerModel::save()
{
    QJsonObject root;
    root["currentAnalyzer"] = m_currentAnalyzer;

    QJsonObject results;
    for (auto it = m_results.begin(); it != m_results.end(); ++it)
        results.insert(it.key(), it.value());
    root["results"] = results;

    QJsonObject resultAnalyzers;
    for (auto it = m_resultAnalyzers.begin(); it != m_resultAnalyzers.end(); ++it)
    {
        if (m_results.contains(it.key()))
            resultAnalyzers.insert(it.key(), it.value());
    }
    root["resultAnalyzers"] = resultAnalyzers;

    QJsonArray history;
    for (const QString &h : m_history)
        history.append(h);
    root["history"] = history;

    QJsonArray pinned;
    for (const QString &p : m_pinned)
        pinned.append(p);
    root["pinned"] = pinned;

    const QString path = storagePath();
    if (path.isEmpty())
        return;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    if (f.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) >= 0)
        (void)f.commit();
}

void AnalyzerModel::load()
{
    const QString path = storagePath();
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return;
    const QJsonObject root = doc.object();

    m_currentAnalyzer = root.value("currentAnalyzer").toString();

    m_results.clear();
    const QJsonObject results = root.value("results").toObject();
    for (auto it = results.begin(); it != results.end(); ++it)
        m_results.insert(it.key(), it.value().toString());

    // `resultAnalyzers` was added after the original results-only format.
    // Missing data is intentionally accepted and leaves the identity empty.
    m_resultAnalyzers.clear();
    const QJsonObject resultAnalyzers = root.value("resultAnalyzers").toObject();
    for (auto it = resultAnalyzers.begin(); it != resultAnalyzers.end(); ++it)
    {
        if (m_results.contains(it.key()))
            m_resultAnalyzers.insert(it.key(), it.value().toString());
    }

    m_history.clear();
    for (const QJsonValue &v : root.value("history").toArray())
        m_history.append(v.toString());

    m_pinned.clear();
    for (const QJsonValue &v : root.value("pinned").toArray())
        m_pinned.append(v.toString());

    emit currentAnalyzerChanged(m_currentAnalyzer);
    emit historyChanged(m_history);
    emit pinnedChanged(m_pinned);
}

void AnalyzerModel::scheduleSave()
{
    if (m_saveTimer)
        m_saveTimer->start(500);
}
