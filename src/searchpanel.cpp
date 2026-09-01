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
#include <QPointer>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

SearchPanel::SearchPanel(QWidget *parent)
    : QWidget(parent), m_engine(std::make_shared<mviewer::core::SearchEngine>()),
      m_alive(std::make_shared<std::atomic<bool>>(true))
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
                emit reindexRequested();
            });
}

SearchPanel::~SearchPanel()
{
    if (m_alive)
        m_alive->store(false);
    if (m_searchTask)
        TaskScheduler::cancel(m_searchTask);
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

void SearchPanel::reindexEntries(const std::vector<mviewer::core::MetadataIndexEntry> &entries)
{
    if (!m_engine)
        return;
    m_engine->indexEntries(entries);
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

    ++m_searchGeneration;
    const uint64_t generation = m_searchGeneration;
    if (m_searchTask)
        TaskScheduler::cancel(m_searchTask);

    if (q.empty())
    {
        m_table->setRowCount(0);
        m_countLabel->clear();
        m_lastResults.clear();
        return;
    }

    // M58: evaluate a value snapshot off the UI thread. A generation token
    // makes this latest-wins: stale searches may finish, but never repaint.
    const auto indexSnapshot = m_engine->snapshot();
    auto alive = m_alive;
    const QPointer<SearchPanel> self(this);
    m_searchTask = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [alive, self, generation, q, indexSnapshot](const TaskScheduler::TaskContext &ctx)
        {
            if (ctx.isCancelled() || !alive->load())
                return;
            const auto results = mviewer::core::SearchEngine::searchSnapshot(indexSnapshot, q);
            if (ctx.isCancelled() || !alive->load() || !self)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [alive, self, generation, results]()
                {
                    if (!alive->load() || !self || generation != self->m_searchGeneration)
                        return;
                    self->m_lastResults = results;
                    self->m_tableGeneration = generation;
                    const int n = static_cast<int>(self->m_lastResults.size());
                    self->m_table->setSortingEnabled(false);
                    self->m_table->setRowCount(n);
                    if (n == 0)
                        self->m_countLabel->setText(
                            "未找到匹配结果 — 试试其他关键词或勾选更多搜索范围");
                    else
                        self->m_countLabel->setText(
                            QString("找到 %1 个结果（点击列头可排序）").arg(n));
                    QTimer::singleShot(0, self.data(),
                                       [self, generation]()
                                       { self->populateResultsChunk(generation, 0); });
                },
                Qt::QueuedConnection);
        });
    if (!m_searchTask)
    {
        m_countLabel->setText("搜索队列繁忙，请稍后重试");
    }
}

void SearchPanel::populateResultsChunk(uint64_t generation, int firstRow)
{
    if (generation != m_tableGeneration || generation != m_searchGeneration)
        return;
    constexpr int kChunk = 256;
    const int end = std::min(firstRow + kChunk, static_cast<int>(m_lastResults.size()));
    for (int i = firstRow; i < end; ++i)
    {
        const auto &r = m_lastResults[static_cast<size_t>(i)];
        const QString fullPath =
            QString::fromUtf8(r.filePath.data(), static_cast<int>(r.filePath.size()));
        const int sepIdx = std::max(fullPath.lastIndexOf('/'), fullPath.lastIndexOf('\\'));
        const QString fname = (sepIdx >= 0) ? fullPath.mid(sepIdx + 1) : fullPath;
        QString typeStr;
        QString snippet;
        if (!r.matches.empty())
        {
            typeStr = matchTypeLabel(r.matches.front().type);
            const auto &snippetUtf8 = r.matches.front().snippet;
            snippet = QString::fromUtf8(snippetUtf8.data(), static_cast<int>(snippetUtf8.size()));
        }
        auto *typeItem = new QTableWidgetItem(typeStr);
        typeItem->setData(Qt::UserRole, fullPath);
        m_table->setItem(i, 0, typeItem);
        m_table->setItem(i, 1, new QTableWidgetItem(fname));
        m_table->setItem(i, 2, new QTableWidgetItem(snippet));
    }
    if (end < static_cast<int>(m_lastResults.size()))
    {
        QTimer::singleShot(0, this,
                           [this, generation, end]()
                           { populateResultsChunk(generation, end); });
    }
    else
    {
        m_table->setSortingEnabled(true);
    }
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
