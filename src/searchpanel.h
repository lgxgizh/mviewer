#pragma once

#include "core/image/RawMetadata.h"
#include "domain/Image.h"
#include "domain/SearchResult.h"

#include <QTableWidget>
#include <QWidget>
#include <memory>
#include <vector>

class QLineEdit;
class QPushButton;
class QCheckBox;
class QLabel;
class QTimer;

namespace mviewer::domain
{
struct SearchQuery;
struct SearchResult;
} // namespace mviewer::domain

namespace mviewer::core
{
class SearchEngine;
}

// SearchPanel is a dockable widget that provides global search across all
// images in the open workspace (filenames, metadata, analyzer output).
// It wraps a SearchEngine instance and displays ranked results.
class SearchPanel : public QWidget
{
    Q_OBJECT
  public:
    explicit SearchPanel(QWidget *parent = nullptr);

    // Supply an external engine so the panel shares state with the caller.
    void setEngine(std::shared_ptr<mviewer::core::SearchEngine> engine);

    // Focus the search input box (e.g. when the shortcut fires).
    void focusSearch();

    // Re-index all known files from scratch. Call when the image set changes.
    void reindex(const std::vector<std::string> &paths,
                 const std::vector<mviewer::domain::ImageMetadata> &metas,
                 const std::vector<mviewer::core::RawMetadata> &raws);

  signals:
    // Emitted when the user clicks a result row; the caller opens the image.
    void resultActivated(const QString &filePath);

  private slots:
    void onSearchTextChanged();
    void onResultDoubleClicked(const QModelIndex &index);

  protected:
    void keyPressEvent(QKeyEvent *event) override;

  private:
    void buildQuery(mviewer::domain::SearchQuery &q) const;
    QString matchTypeLabel(mviewer::domain::SearchMatch::Type type) const;
    void performSearch();

    QLineEdit *m_searchEdit = nullptr;
    QCheckBox *m_chkFilename = nullptr;
    QCheckBox *m_chkMetadata = nullptr;
    QCheckBox *m_chkAnalysis = nullptr;
    QCheckBox *m_chkPaths = nullptr;
    QLabel *m_countLabel = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_reindexBtn = nullptr;
    QTimer *m_debounceTimer = nullptr;

    std::shared_ptr<mviewer::core::SearchEngine> m_engine;
    std::vector<mviewer::domain::SearchResult> m_lastResults;
};
