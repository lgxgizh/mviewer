#include "compareworkspace_pane_header.h"

#include "compareworkspace_caption.h"
#include "compareworkspace_p.h"

#include "core/image/ImageFrame.h"
#include "widgets/infooverlay.h"

#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace
{

constexpr int kPad = 8;
constexpr int kChip = 24;
constexpr int kOuter = 6; // gap between the overlay and the pane edge

// Camera-style names ("DSC_2026_10_13_..._v3.jpg") have no spaces, so a
// word-wrapping QLabel cannot break them. Offer a zero-width break after the
// usual separators (and inside very long unbroken runs) so long names
// really wrap instead of being clipped.
QString wrappableName(const QString &name)
{
    QString out;
    out.reserve(name.size() + name.size() / 4);
    int run = 0; // characters since the last break opportunity
    for (const QChar ch : name)
    {
        out.append(ch);
        ++run;
        const bool separator =
            ch == QLatin1Char('_') || ch == QLatin1Char('-') || ch == QLatin1Char('.');
        if (ch.isSpace())
            run = 0;
        else if (separator || run >= 16)
        {
            out.append(QChar(0x200B));
            run = 0;
        }
    }
    return out;
}

// The pane header (index chip + file name) is an OVERLAY floating over the
// top-left of the image: it is not in the cell layout, so it takes no space
// and every pane's image viewport is identical whatever the name length.
// (It used to be a layout row that grew to two lines for long names, which
// shortened that pane's viewport and made its image fit visibly smaller.)
// Long names wrap onto as many lines as needed; nothing is elided.
class ComparePaneHeader : public QWidget
{
  public:
    explicit ComparePaneHeader(QWidget *parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("comparePaneHeader"));
        setAttribute(Qt::WA_StyledBackground, true);
        // Pan/zoom/hover on the image keep working under the overlay.
        setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(kPad, kPad, kPad, kPad);
        row->setSpacing(kPad);

        m_badge = new QLabel(this);
        m_badge->setObjectName(QStringLiteral("paneIndexBadge"));
        m_badge->setAlignment(Qt::AlignCenter);
        m_badge->setFixedSize(kChip, kChip);
        m_badge->setStyleSheet(
            QStringLiteral("QLabel#paneIndexBadge{background:rgba(255,255,255,45);color:#ffffff;"
                           "border-radius:6px;font-weight:700;font-size:14px;}"));
        m_badge->hide();
        row->addWidget(m_badge, 0, Qt::AlignTop);

        m_name = new QLabel(this);
        m_name->setObjectName(QStringLiteral("paneHeaderName"));
        m_name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        QFont nameFont = m_name->font();
        nameFont.setBold(true);
        nameFont.setPointSize(mviewer::ui::kFilenameOverlayPointSize);
        m_name->setFont(nameFont);
        m_name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        m_name->setMinimumWidth(0);
        m_name->setStyleSheet(QStringLiteral(
            "QLabel#paneHeaderName{color:#ffffff;font-weight:700;background:transparent;}"));
        row->addWidget(m_name, 1);

        setStyleSheet(QStringLiteral("QWidget#comparePaneHeader{background:rgba(0,0,0,%1);"
                                     "border-radius:%2px;}")
                          .arg(mviewer::ui::kOverlayChipAlpha)
                          .arg(mviewer::ui::kOverlayChipRadius));
        if (parent)
            parent->installEventFilter(this);
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
        updateChrome();
    }

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == parentWidget() &&
            (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest ||
             event->type() == QEvent::Show))
            relayout();
        return QWidget::eventFilter(watched, event);
    }

  private:
    void updateChrome()
    {
        const bool badgeOn = m_badge && !m_badge->isHidden();
        const bool nameOn = m_showName && !m_fullName.isEmpty();
        setVisible(badgeOn || nameOn);
        relayout();
    }

    // Size the overlay to its content, wrapping the name to the pane width,
    // and float it over the pane's top-left corner above the image view.
    void relayout()
    {
        QWidget *cell = parentWidget();
        if (!cell || !m_name || m_refreshing)
            return;
        m_refreshing = true;
        const bool badgeOn = m_badge && !m_badge->isHidden();
        const bool nameOn = m_showName && !m_fullName.isEmpty();
        const QFontMetrics metrics(m_name->font());
        const int chipW = badgeOn ? kChip : 0;
        const int gap = badgeOn && nameOn ? kPad : 0;
        const int avail = qMax(kChip + kPad * 2, cell->width() - kOuter * 2);
        const int nameMax = qMax(8, avail - kPad * 2 - chipW - gap);
        int nameW = 0;
        int nameH = 0;
        if (nameOn)
        {
            const int natural = metrics.horizontalAdvance(m_fullName) + 2;
            const bool wrap = natural > nameMax;
            m_name->setWordWrap(wrap);
            m_name->setText(wrap ? wrappableName(m_fullName) : m_fullName);
            nameW = wrap ? nameMax : natural;
            nameH = wrap ? qMax(metrics.height(), m_name->heightForWidth(nameW)) : metrics.height();
        }
        const int w = kPad * 2 + chipW + gap + nameW;
        const int h = kPad * 2 + qMax(badgeOn ? kChip : 0, nameH);
        setFixedSize(qMin(w, avail), h);
        move(kOuter, kOuter);
        raise();
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
    // Overlay, not a layout row: the layout is deliberately left untouched.
    return new ComparePaneHeader(cell);
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
        // Name the image this pane presents now; a temporary switch shows the
        // other pane's image, so its name (and path) come from that pane.
        const int shown = displayedImageIndex(i);
        QString name;
        if (shown >= 0 && shown < m_cellLabels.size())
            name = comparePaneCaptionFullText(m_cellLabels[shown]);
        QString path = name;
        if (const ImageFrame *img = m_engine.imageAt(shown))
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
        if (i >= m_cellLabels.size() || !m_cellLabels[i])
            continue;
        // A status caption describes this pane's own image: hide it while the
        // pane shows another image and bring it back on release.
        QLabel *caption = m_cellLabels[i];
        const char *hiddenKey = "mvHiddenForSwitch";
        if (!comparePaneCaptionHasStatus(caption))
            caption->setVisible(false);
        else if (shown != i && caption->isVisibleTo(caption->parentWidget()))
        {
            caption->setProperty(hiddenKey, true);
            caption->setVisible(false);
        }
        else if (shown == i && caption->property(hiddenKey).toBool())
        {
            caption->setProperty(hiddenKey, false);
            caption->setVisible(true);
        }
    }
}
