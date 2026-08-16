#pragma once

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

// M19: single source of truth for analysis results and analyzer selection.
//
// Before this class, per-image analysis text lived only in
// MainWindow::m_analysisByPath, and the AnalysisPanel held its own combo
// selection with no shared history/pin state. Downstream Export / Workspace
// persistence had to scrape the panel.
//
// This model owns:
//   * current analyzer id (registry key)
//   * per-image result text + producer analyzer id (capped)
//   * recent analysis history (image paths, most-recent first)
//   * pinned results (image paths the user wants to keep)
//
// Intentionally tiny: holds state and emits change signals. Running analyzers
// still goes through AnalyzerPipeline / AnalysisPanel.
class AnalyzerModel : public QObject
{
    Q_OBJECT
  public:
    static constexpr int kMaxResults = 500;
    static constexpr int kMaxHistory = 50;

    explicit AnalyzerModel(QObject *parent = nullptr);

    QString currentAnalyzerId() const
    {
        return m_currentAnalyzer;
    }
    QString resultText(const QString &imagePath) const
    {
        return m_results.value(imagePath);
    }
    // Analyzer identity is stored per path with the result producer.  A
    // global current selection is not sufficient: revisiting an older image
    // after changing analyzers must never relabel its result.
    QString resultAnalyzerId(const QString &imagePath) const
    {
        return m_resultAnalyzers.value(imagePath);
    }
    QMap<QString, QString> allResults() const
    {
        return m_results;
    }
    QStringList history() const
    {
        return m_history;
    }
    QStringList pinned() const
    {
        return m_pinned;
    }
    bool isPinned(const QString &imagePath) const
    {
        return m_pinned.contains(imagePath);
    }

  public slots:
    void setCurrentAnalyzer(const QString &id);
    void setResult(const QString &imagePath, const QString &text);
    void setResult(const QString &imagePath, const QString &text, const QString &analyzerId);
    void clearResult(const QString &imagePath);
    void clearAllResults();
    void pinResult(const QString &imagePath);
    void unpinResult(const QString &imagePath);
    void pushHistory(const QString &imagePath);

    // Persist analysis history / pinned / last results to disk so they survive
    // restarts. save() writes immediately; load() repopulates the model;
    // scheduleSave() debounces writes triggered by the change signals below.
    void save();
    void load();
    void scheduleSave();

  signals:
    void currentAnalyzerChanged(const QString &id);
    void resultChanged(const QString &imagePath, const QString &text);
    void historyChanged(const QStringList &paths);
    void pinnedChanged(const QStringList &paths);

  private:
    QString storagePath() const;
    QString m_currentAnalyzer;
    QMap<QString, QString> m_results;
    QMap<QString, QString> m_resultAnalyzers;
    QStringList m_history;
    QStringList m_pinned;
    QTimer *m_saveTimer = nullptr;
};
