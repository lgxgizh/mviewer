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
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QPointer>
#include <QTreeView>
#include <QVBoxLayout>

#include <cstdint>

MetadataPanel::MetadataPanel(QWidget *parent) : QWidget(parent)
{
    m_consumerId = "metadata-panel-" + std::to_string(reinterpret_cast<std::uintptr_t>(this));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    layout->addWidget(new QLabel(tr("元数据 (Metadata)"), this));

    // "复制全部" button: dumps the full hierarchical model to the clipboard as
    // "Key: Value" lines — handy for sharing EXIF info or pasting into reports.
    // M24 (Phase 5): no dedicated shortcut here — Ctrl+Shift+C is reserved for
    // the gallery's copy-path action; a button + tooltip suffices for this
    // low-frequency diagnostic action.
    auto *topBar = new QHBoxLayout();
    topBar->addStretch(1);
    auto *copyBtn = new QPushButton(tr("复制全部"), this);
    copyBtn->setToolTip(tr("复制全部元数据到剪贴板"));
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
                rs.setRating(m_currentPath.toUtf8().toStdString(), stars);
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
        emit flagsEdited(m_currentPath, rs.colorLabel(m_currentPath.toUtf8().toStdString()),
                         rs.rejected(m_currentPath.toUtf8().toStdString()),
                         rs.picked(m_currentPath.toUtf8().toStdString()));
    };

    connect(m_colorLabel, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, emitFlags](int)
            {
                if (m_currentPath.isEmpty())
                    return;
                auto &rs = mviewer::core::RatingStore::instance();
                const int label = m_colorLabel->currentData().toInt();
                rs.setColorLabel(m_currentPath.toUtf8().toStdString(), label);
                emitFlags();
            });
    connect(m_rejectBtn, &QPushButton::toggled, this,
            [this, emitFlags](bool on)
            {
                if (m_currentPath.isEmpty())
                    return;
        mviewer::core::RatingStore::instance().setRejected(m_currentPath.toUtf8().toStdString(), on);
                emitFlags();
            });
    connect(m_pickBtn, &QPushButton::toggled, this,
            [this, emitFlags](bool on)
            {
                if (m_currentPath.isEmpty())
                    return;
        mviewer::core::RatingStore::instance().setPicked(m_currentPath.toUtf8().toStdString(), on);
                emitFlags();
            });
    layout->addWidget(flagBox);

    // Until an image is selected, the model shows its "select an image" hint.
    m_model->clear();
}

MetadataPanel::~MetadataPanel()
{
    mviewer::core::MetadataPresentationService::instance().cancel(m_consumerId);
}

void MetadataPanel::hideEvent(QHideEvent *event)
{
    mviewer::core::MetadataPresentationService::instance().cancel(m_consumerId);
    ++m_requestGeneration;
    QWidget::hideEvent(event);
}

void MetadataPanel::setImage(const QString &path)
{
    m_currentPath = path;
    ++m_requestGeneration;

    auto &rs = mviewer::core::RatingStore::instance();
    m_rating->setRating(rs.rating(path.toUtf8().toStdString()));
    m_colorLabel->setCurrentIndex(m_colorLabel->findData(rs.colorLabel(path.toUtf8().toStdString())));
    m_rejectBtn->setChecked(!path.isEmpty() && rs.rejected(path.toUtf8().toStdString()));
    m_pickBtn->setChecked(!path.isEmpty() && rs.picked(path.toUtf8().toStdString()));

    if (path.isEmpty())
    {
        clear();
        return;
    }

    if (!isVisible())
    {
        // SelectionModel notifies this panel even while it is hidden. Do not
        // keep an older presentation flight alive just because the hidden
        // widget recorded a newer identity.
        mviewer::core::MetadataPresentationService::instance().cancel(m_consumerId);
        m_model->clear();
        return;
    }
    requestMetadata();
}

void MetadataPanel::setFrame(const std::shared_ptr<ImageFrame> &frame)
{
    if (!frame || frame->metadata().filePath != m_currentPath || m_currentPath.isEmpty())
        return;
    // The metadata service supplies the container/EXIF tree. Replace only the
    // image metadata on frame changes so Current Frame/Page follows playback
    // without starting a metadata file read for every animation tick.
    m_model->setImage(frame->metadata());
    m_tree->expandAll();
}

void MetadataPanel::requestMetadata()
{
    const QString path = m_currentPath;
    const uint64_t generation = m_requestGeneration;
    QPointer<MetadataPanel> guard(this);
    mviewer::core::MetadataPresentationService::instance().request(
        path.toUtf8().toStdString(), m_consumerId,
        [guard, path, generation](const mviewer::core::MetadataPresentationService::Snapshot &snapshot)
        {
            if (!guard || !guard->isVisible() || guard->m_currentPath != path ||
                guard->m_requestGeneration != generation)
                return;
            const auto &meta = snapshot.metadata;
            if (meta.filePath.empty())
            {
                guard->m_model->clear();
                guard->m_model->setImage(meta);
                return;
            }
            guard->m_model->setImage(meta);
            guard->m_model->setRaw(snapshot.raw);
            guard->m_tree->expandAll();
            guard->m_tree->setColumnWidth(0, 130);
        });
}

/*
    The presentation service owns the single background read. Keep the old
    model update shape below only in the service callback above.
    The panel itself never opens the image path.
*/
/*
    const mviewer::domain::ImageMetadata meta =
        mviewer::core::MetadataReader::read(path.toUtf8().toStdString());
    if (meta.filePath.empty())
    {
        // P0#4: the model owns all rendering, including the error row.
        m_model->clear();
        m_model->setImage(meta); // empty meta → model shows the hint
        return;
    }

    m_model->setImage(meta);

    // M14-2: if the file is a RAW format, also show sensor metadata.
    const mviewer::core::RawMetadata rm = mviewer::core::parseRawMetadata(path.toUtf8().toStdString());
    m_model->setRaw(rm);

    m_tree->expandAll();
    m_tree->setColumnWidth(0, 130);
*/

void MetadataPanel::clear()
{
    mviewer::core::MetadataPresentationService::instance().cancel(m_consumerId);
    ++m_requestGeneration;
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
            lines << QString("  %1: %2").arg(keyIdx.data().toString(), valIdx.data().toString());
        }
    }
    if (lines.isEmpty())
        return;
    QApplication::clipboard()->setText(lines.join('\n'));
}
