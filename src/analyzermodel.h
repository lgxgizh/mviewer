#pragma once

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

// M19: single source of truth for analysis results and analyzer selection.
//
// Before this class, per-image analysis text lived only in
// MainWindow::m_analysisByPath, and the AnalysisPanel held its own combo
// selection with no shared history/pin state. Downstream Export / Workspace
// persistence had to scrape the panel.
//
// This model owns:
//   * current analyzer id (registry key)
//   * per-image result text (capped)
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
    void clearResult(const QString &imagePath);
    void clearAllResults();
    void pinResult(const QString &imagePath);
    void unpinResult(const QString &imagePath);
    void pushHistory(const QString &imagePath);

  signals:
    void currentAnalyzerChanged(const QString &id);
    void resultChanged(const QString &imagePath, const QString &text);
    void historyChanged(const QStringList &paths);
    void pinnedChanged(const QStringList &paths);

  private:
    QString m_currentAnalyzer;
    QMap<QString, QString> m_results;
    QStringList m_history;
    QStringList m_pinned;
};
