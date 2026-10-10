#include "compareworkspace_pane_header.h"

#include "compareworkspace_caption.h"
#include "compareworkspace_p.h"

#include "Theme.h"
#include "core/image/ImageFrame.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace
{

constexpr int kPad = 8;
constexpr int kChip = 22;

int oneLineHeight(const QFontMetrics &metrics)
{
    const int textHeight = metrics.height() + kPad * 2;
    const int chipHeight = kChip + kPad * 2;
    return textHeight > chipHeight ? textHeight : chipHeight;
}

class ComparePaneHeader : public QWidget
{
  public:
    explicit ComparePaneHeader(QWidget *parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("comparePaneHeader"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(kPad, kPad, kPad, kPad);
        row->setSpacing(kPad);

        m_badge = new QLabel(this);
        m_badge->setObjectName(QStringLiteral("paneIndexBadge"));
        m_badge->setAlignment(Qt::AlignCenter);
        m_badge->setFixedSize(kChip, kChip);
        m_badge->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_badge->setStyleSheet(QStringLiteral(
            "QLabel#paneIndexBadge{background:rgba(0,0,0,120);color:rgba(255,255,255,210);"
            "border-radius:3px;font-weight:700;font-size:13px;}"));
        m_badge->hide();
        row->addWidget(m_badge, 0, Qt::AlignVCenter);

        m_name = new QLabel(this);
        m_name->setObjectName(QStringLiteral("paneHeaderName"));
        m_name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_name->setMinimumWidth(0);
        m_name->setStyleSheet(
            QStringLiteral("QLabel#paneHeaderName{color:%1;background:transparent;}")
                .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextPrimary)));
        row->addWidget(m_name, 1, Qt::AlignVCenter);

        setStyleSheet(QStringLiteral("QWidget#comparePaneHeader{background:%1;}")
                          .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::Bg1)));
        setFixedHeight(oneLineHeight(QFontMetrics(m_name->font())));
        hide();
    }

    void setBadge(int index, bool show)
    {
        if (!m_badge)
            return;
        m_badge->setText(QString::number(index));
        m_badge->setVisible(show);
        updateChrome();
    }

    void setName(const QString &name, const QString &fullPath, bool showName)
    {
        m_fullName = name;
        m_fullPath = fullPath.isEmpty() ? name : fullPath;
        m_showName = showName && !name.isEmpty();
        if (m_name)
        {
            m_name->setVisible(m_showName);
            m_name->setToolTip(m_fullPath);
        }
        refreshName();
        updateChrome();
    }

  protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        refreshName();
    }

  private:
    void updateChrome()
    {
        const bool badgeOn = m_badge && !m_badge->isHidden();
        const bool nameOn = m_showName && !m_fullName.isEmpty();
        setVisible(badgeOn || nameOn);
    }

    void refreshName()
    {
        if (!m_name || m_refreshing)
            return;
        m_refreshing = true;
        const QFontMetrics metrics(m_name->font());
        const int line = oneLineHeight(metrics);
        const int width = m_name->width();
        if (!m_showName || m_fullName.isEmpty() || width < 8)
        {
            m_name->setWordWrap(false);
            m_name->setText(m_fullName);
            if (height() != line)
                setFixedHeight(line);
            m_refreshing = false;
            return;
        }
        const int fullWidth = metrics.horizontalAdvance(m_fullName);
        if (fullWidth <= width)
        {
            m_name->setWordWrap(false);
            m_name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            m_name->setText(m_fullName);
            if (height() != line)
                setFixedHeight(line);
        }
        else if (fullWidth <= width * 2)
        {
            const int twoLines = line + metrics.height();
            m_name->setWordWrap(true);
            m_name->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            m_name->setText(m_fullName);
            if (height() != twoLines)
                setFixedHeight(twoLines);
        }
        else
        {
            m_name->setWordWrap(false);
            m_name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            m_name->setText(metrics.elidedText(m_fullName, Qt::ElideMiddle, width));
            if (height() != line)
                setFixedHeight(line);
        }
        m_refreshing = false;
    }

    QLabel *m_badge = nullptr;
    QLabel *m_name = nullptr;
    QString m_fullName;
    QString m_fullPath;
    bool m_showName = false;
    bool m_refreshing = false;
};

ComparePaneHeader *headerOn(QWidget *cell)
{
    QWidget *widget =
        cell ? cell->findChild<QWidget *>(QStringLiteral("comparePaneHeader")) : nullptr;
    return static_cast<ComparePaneHeader *>(widget);
}

} // namespace

namespace mviewer::ui
{

QWidget *installComparePaneHeader(QWidget *cell, QVBoxLayout *layout)
{
    if (!cell || !layout)
        return nullptr;
    auto *header = new ComparePaneHeader(cell);
    layout->insertWidget(0, header);
    return header;
}

void updateComparePaneHeaderName(QWidget *cell, const QString &name, const QString &fullPath,
                                 bool showName)
{
    if (ComparePaneHeader *header = headerOn(cell))
        header->setName(name, fullPath, showName);
}

void updateComparePaneHeaderBadge(QWidget *cell, int indexOneBased, bool show)
{
    if (ComparePaneHeader *header = headerOn(cell))
        header->setBadge(indexOneBased, show);
}

} // namespace mviewer::ui

void CompareWorkspace::applyFilenameOverlays()
{
    for (int i = 0; i < m_cellViews.size(); ++i)
    {
        RawImageView *view = m_cellViews[i];
        if (!view)
            continue;
        QString name;
        if (i < m_cellLabels.size())
            name = comparePaneCaptionFullText(m_cellLabels[i]);
        QString path = name;
        if (const ImageFrame *img = m_engine.imageAt(i))
        {
            if (name.isEmpty() && !img->metadata().fileName.empty())
                name = QString::fromStdString(img->metadata().fileName);
            if (!img->metadata().filePath.empty())
                path = QString::fromStdString(img->metadata().filePath);
            else if (!img->metadata().fileName.empty())
                path = QString::fromStdString(img->metadata().fileName);
        }
        view->setFilenameOverlay(name, m_filenameOverlay);
        mviewer::ui::updateComparePaneHeaderName(view->parentWidget(), name, path,
                                                 m_filenameOverlay);
        if (i < m_cellLabels.size() && m_cellLabels[i] &&
            !comparePaneCaptionHasStatus(m_cellLabels[i]))
            m_cellLabels[i]->setVisible(false);
    }
}
