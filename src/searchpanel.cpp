#include "searchpanel.h"
#include "core/search/SearchEngine.h"
#include "domain/SearchQuery.h"
#include "domain/SearchResult.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

SearchPanel::SearchPanel(QWidget *parent)
    : QWidget(parent), m_engine(std::make_shared<mviewer::core::SearchEngine>())
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // ── top bar: search input + reindex button ──────────────────────
    auto *topBar = new QHBoxLayout;
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText("搜索文件名、元数据、分析结果...");
    m_searchEdit->setClearButtonEnabled(true);
    topBar->addWidget(m_searchEdit);

    m_reindexBtn = new QPushButton("重建索引");
    m_reindexBtn->setToolTip("重新扫描所有图像构建搜索索引");
    m_reindexBtn->setFixedWidth(72);
    topBar->addWidget(m_reindexBtn);
    mainLayout->addLayout(topBar);

    // ── scope checkboxes ────────────────────────────────────────────
    auto *scopeBar = new QHBoxLayout;
    m_chkFilename = new QCheckBox("文件名");
    m_chkFilename->setChecked(true);
    m_chkMetadata = new QCheckBox("元数据");
    m_chkMetadata->setChecked(true);
    m_chkAnalysis = new QCheckBox("分析结果");
    m_chkAnalysis->setChecked(true);
    m_chkPaths = new QCheckBox("路径");
    m_chkPaths->setChecked(false);
    scopeBar->addWidget(m_chkFilename);
    scopeBar->addWidget(m_chkMetadata);
    scopeBar->addWidget(m_chkAnalysis);
    scopeBar->addWidget(m_chkPaths);
    scopeBar->addStretch();
    mainLayout->addLayout(scopeBar);

    // ── result count label ──────────────────────────────────────────
    m_countLabel = new QLabel;
    mainLayout->addWidget(m_countLabel);

    // ── result table ────────────────────────────────────────────────
    m_table = new QTableWidget(0, 3);
    m_table->setHorizontalHeaderLabels({"类型", "文件名", "匹配片段"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Click a column header to sort the results by that column.
    m_table->setSortingEnabled(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(false);
    mainLayout->addWidget(m_table);

    // ── connections ─────────────────────────────────────────────────
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(250);
    connect(m_debounceTimer, &QTimer::timeout, this, &SearchPanel::performSearch);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &SearchPanel::onSearchTextChanged);
    connect(m_table, &QTableWidget::doubleClicked, this, &SearchPanel::onResultDoubleClicked);
    connect(m_chkFilename, &QCheckBox::toggled, this, &SearchPanel::onSearchTextChanged);
    connect(m_chkMetadata, &QCheckBox::toggled, this, &SearchPanel::onSearchTextChanged);
    connect(m_chkAnalysis, &QCheckBox::toggled, this, &SearchPanel::onSearchTextChanged);
    connect(m_chkPaths, &QCheckBox::toggled, this, &SearchPanel::onSearchTextChanged);
    connect(m_reindexBtn, &QPushButton::clicked, this,
            [this]()
            {
                if (m_engine)
                {
                    // Re-index is handled externally via reindex().
                    // Re-run the current filter to refresh the table.
                    onSearchTextChanged();
                }
            });
}

void SearchPanel::setEngine(std::shared_ptr<mviewer::core::SearchEngine> engine)
{
    m_engine = std::move(engine);
    onSearchTextChanged();
}

void SearchPanel::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
    {
        m_searchEdit->clear();
        m_searchEdit->setFocus();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void SearchPanel::focusSearch()
{
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

void SearchPanel::reindex(const std::vector<std::string> &paths,
                          const std::vector<mviewer::domain::ImageMetadata> &metas,
                          const std::vector<mviewer::core::RawMetadata> &raws)
{
    if (!m_engine)
        return;
    m_engine->indexDirectory(paths, metas, raws, {});
    onSearchTextChanged();
}

void SearchPanel::onSearchTextChanged()
{
    // Debounce: restart the timer so we only search after the user pauses
    // typing for 250ms, avoiding lag on large indexes.
    m_debounceTimer->start();
}

void SearchPanel::performSearch()
{
    if (!m_engine)
        return;

    mviewer::domain::SearchQuery q;
    buildQuery(q);

    if (q.empty())
    {
        m_table->setRowCount(0);
        m_countLabel->clear();
        m_lastResults.clear();
        return;
    }

    m_lastResults = m_engine->search(q);

    const int n = static_cast<int>(m_lastResults.size());
    m_table->setSortingEnabled(false); // avoid resorting mid-populate
    m_table->setRowCount(n);

    if (n == 0)
    {
        m_countLabel->setText("未找到匹配结果 — 试试其他关键词或勾选更多搜索范围");
    }
    else
    {
        m_countLabel->setText(QString("找到 %1 个结果（点击列头可排序）").arg(n));
    }

    for (int i = 0; i < n; ++i)
    {
        const auto &r = m_lastResults[static_cast<size_t>(i)];

        // Extract filename from path.
        const QString fullPath = QString::fromStdString(r.filePath);
        const int sepIdx = std::max(fullPath.lastIndexOf('/'), fullPath.lastIndexOf('\\'));
        const QString fname = (sepIdx >= 0) ? fullPath.mid(sepIdx + 1) : fullPath;

        // Best match type and snippet.
        QString typeStr;
        QString snippet;
        if (!r.matches.empty())
        {
            typeStr = matchTypeLabel(r.matches.front().type);
            snippet = QString::fromStdString(r.matches.front().snippet);
        }

        auto *typeItem = new QTableWidgetItem(typeStr);
        typeItem->setData(Qt::UserRole, fullPath);
        m_table->setItem(i, 0, typeItem);

        m_table->setItem(i, 1, new QTableWidgetItem(fname));
        m_table->setItem(i, 2, new QTableWidgetItem(snippet));
    }
    m_table->setSortingEnabled(true); // re-enable so header-click sorting works
}

void SearchPanel::onResultDoubleClicked(const QModelIndex &index)
{
    auto *item = m_table->item(index.row(), 0);
    if (!item)
        return;
    const QString path = item->data(Qt::UserRole).toString();
    if (!path.isEmpty())
        emit resultActivated(path);
}

void SearchPanel::buildQuery(mviewer::domain::SearchQuery &q) const
{
    q.text = m_searchEdit->text().trimmed().toStdString();
    q.searchFilenames = m_chkFilename->isChecked();
    q.searchMetadata = m_chkMetadata->isChecked();
    q.searchAnalysis = m_chkAnalysis->isChecked();
    q.searchPaths = m_chkPaths->isChecked();
}

QString SearchPanel::matchTypeLabel(mviewer::domain::SearchMatch::Type type) const
{
    switch (type)
    {
    case mviewer::domain::SearchMatch::Type::Filename:
        return "文件名";
    case mviewer::domain::SearchMatch::Type::Metadata:
        return "元数据";
    case mviewer::domain::SearchMatch::Type::Analysis:
        return "分析结果";
    case mviewer::domain::SearchMatch::Type::Path:
        return "路径";
    }
    return {};
}
