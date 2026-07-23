#include "metadatapanel.h"

#include "core/RatingStore.h"
#include "core/image/MetadataReader.h"
#include "metadatamodel.h"
#include "widgets/ratingwidget.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>

MetadataPanel::MetadataPanel(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    layout->addWidget(new QLabel(tr("元数据 (Metadata)"), this));

    // "复制全部" button: dumps the full hierarchical model to the clipboard as
    // "Key: Value" lines — handy for sharing EXIF info or pasting into reports.
    auto *topBar = new QHBoxLayout();
    topBar->addStretch(1);
    auto *copyBtn = new QPushButton(tr("复制全部"), this);
    copyBtn->setToolTip(tr("复制全部元数据到剪贴板 (Ctrl+Shift+C)"));
    copyBtn->setShortcut(QKeySequence("Ctrl+Shift+C"));
    topBar->addWidget(copyBtn);
    connect(copyBtn, &QPushButton::clicked, this, &MetadataPanel::copyAll);
    layout->addLayout(topBar);

    // P0#4: a single unified model drives the tree view.
    m_model = new MetadataModel(this);
    m_tree = new QTreeView(this);
    m_tree->setModel(m_model);
    m_tree->setAlternatingRowColors(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->header()->setStretchLastSection(true);
    layout->addWidget(m_tree, 1);

    // P1: star-rating editor, persists to RatingStore.
    auto *ratingBox = new QWidget(this);
    auto *ratingLay = new QHBoxLayout(ratingBox);
    ratingLay->setContentsMargins(0, 0, 0, 0);
    ratingLay->setSpacing(6);
    ratingLay->addWidget(new QLabel(tr("评分:"), this));
    m_rating = new RatingWidget(this);
    ratingLay->addWidget(m_rating);
    ratingLay->addStretch(1);
    connect(m_rating, &RatingWidget::ratingChanged, this,
            [this](int stars)
            {
                if (m_currentPath.isEmpty())
                    return;
                auto &rs = mviewer::core::RatingStore::instance();
                rs.setRating(m_currentPath.toStdString(), stars);
                emit ratingEdited(m_currentPath, stars);
            });
    layout->addWidget(ratingBox);

    // P3 tail: color label + reject / pick (favorite) controls.
    auto *flagBox = new QWidget(this);
    auto *flagLay = new QHBoxLayout(flagBox);
    flagLay->setContentsMargins(0, 0, 0, 0);
    flagLay->setSpacing(6);
    flagLay->addWidget(new QLabel(tr("色标:"), this));
    m_colorLabel = new QComboBox(this);
    m_colorLabel->addItem(tr("无"), 0);
    m_colorLabel->addItem(tr("红"), 1);
    m_colorLabel->addItem(tr("橙"), 2);
    m_colorLabel->addItem(tr("黄"), 3);
    m_colorLabel->addItem(tr("绿"), 4);
    m_colorLabel->addItem(tr("蓝"), 5);
    m_colorLabel->addItem(tr("紫"), 6);
    flagLay->addWidget(m_colorLabel);
    m_rejectBtn = new QPushButton(tr("拒绝"), this);
    m_rejectBtn->setCheckable(true);
    m_pickBtn = new QPushButton(tr("收藏"), this);
    m_pickBtn->setCheckable(true);
    flagLay->addWidget(m_rejectBtn);
    flagLay->addWidget(m_pickBtn);
    flagLay->addStretch(1);

    const auto emitFlags = [this]
    {
        if (m_currentPath.isEmpty())
            return;
        auto &rs = mviewer::core::RatingStore::instance();
        emit flagsEdited(m_currentPath, rs.colorLabel(m_currentPath.toStdString()),
                         rs.rejected(m_currentPath.toStdString()),
                         rs.picked(m_currentPath.toStdString()));
    };

    connect(m_colorLabel, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, emitFlags](int)
            {
                if (m_currentPath.isEmpty())
                    return;
                auto &rs = mviewer::core::RatingStore::instance();
                const int label = m_colorLabel->currentData().toInt();
                rs.setColorLabel(m_currentPath.toStdString(), label);
                emitFlags();
            });
    connect(m_rejectBtn, &QPushButton::toggled, this,
            [this, emitFlags](bool on)
            {
                if (m_currentPath.isEmpty())
                    return;
                mviewer::core::RatingStore::instance().setRejected(m_currentPath.toStdString(), on);
                emitFlags();
            });
    connect(m_pickBtn, &QPushButton::toggled, this,
            [this, emitFlags](bool on)
            {
                if (m_currentPath.isEmpty())
                    return;
                mviewer::core::RatingStore::instance().setPicked(m_currentPath.toStdString(), on);
                emitFlags();
            });
    layout->addWidget(flagBox);

    // Until an image is selected, the model shows its "select an image" hint.
    m_model->clear();
}

void MetadataPanel::setImage(const QString &path)
{
    m_currentPath = path;

    auto &rs = mviewer::core::RatingStore::instance();
    m_rating->setRating(rs.rating(path.toStdString()));
    m_colorLabel->setCurrentIndex(m_colorLabel->findData(rs.colorLabel(path.toStdString())));
    m_rejectBtn->setChecked(!path.isEmpty() && rs.rejected(path.toStdString()));
    m_pickBtn->setChecked(!path.isEmpty() && rs.picked(path.toStdString()));

    if (path.isEmpty())
    {
        clear();
        return;
    }

    const mviewer::domain::ImageMetadata meta =
        mviewer::core::MetadataReader::read(path.toStdString());
    if (meta.filePath.empty())
    {
        // P0#4: the model owns all rendering, including the error row.
        m_model->clear();
        m_model->setImage(meta); // empty meta → model shows the hint
        return;
    }

    m_model->setImage(meta);

    // M14-2: if the file is a RAW format, also show sensor metadata.
    const mviewer::core::RawMetadata rm = mviewer::core::parseRawMetadata(path.toStdString());
    m_model->setRaw(rm);

    m_tree->expandAll();
    m_tree->setColumnWidth(0, 130);
}

void MetadataPanel::clear()
{
    m_currentPath.clear();
    m_model->clear();
    m_rating->setRating(0);
    m_colorLabel->setCurrentIndex(0);
    m_rejectBtn->setChecked(false);
    m_pickBtn->setChecked(false);
}

void MetadataPanel::copyAll()
{
    // Walk the model's top-level categories and their leaf rows, emitting
    // "Category / Key: Value" lines for the clipboard.
    QStringList lines;
    const QModelIndex root;
    for (int cat = 0; cat < m_model->rowCount(root); ++cat)
    {
        const QModelIndex catIdx = m_model->index(cat, 0, root);
        const QString catName = catIdx.data().toString();
        if (!catName.isEmpty())
            lines << QString("[%1]").arg(catName);
        for (int r = 0; r < m_model->rowCount(catIdx); ++r)
        {
            const QModelIndex keyIdx = m_model->index(r, 0, catIdx);
            const QModelIndex valIdx = m_model->index(r, 1, catIdx);
            lines << QString("  %1: %2")
                       .arg(keyIdx.data().toString(), valIdx.data().toString());
        }
    }
    if (lines.isEmpty())
        return;
    QApplication::clipboard()->setText(lines.join('\n'));
}
