#include "compareworkspace_p.h"
#include "widgets/roioverlay.h"

#include <algorithm>
#include <cmath>

namespace
{
QString ratioChip(double value, bool valid)
{
    return valid ? QString::number(value, 'f', 4) : QStringLiteral("—");
}

QString roiChipText(const std::optional<mviewer::ui::ROIStatsBatchResult> &result, int index)
{
    if (!result || index < 0 || index >= static_cast<int>(result->panes.size()))
        return QStringLiteral("R/G …\nH …  S …  V …");
    const auto &pane = result->panes[static_cast<size_t>(index)];
    if (!pane.stats.valid)
        return QStringLiteral("R/G —\nH —  S —  V —");
    const auto &stats = pane.stats;
    return QStringLiteral("R/G %1  B/G %2\nH %3  S %4  V %5  R %6  G %7  B %8")
        .arg(ratioChip(stats.rOverG, stats.ratiosValid), ratioChip(stats.bOverG, stats.ratiosValid),
             QString::number(stats.hMean, 'f', 1), QString::number(stats.sMean, 'f', 1),
             QString::number(stats.vMean, 'f', 1), QString::number(stats.rMean, 'f', 1),
             QString::number(stats.gMean, 'f', 1), QString::number(stats.bMean, 'f', 1));
}

QLabel *roiChip(QWidget *owner, int index)
{
    const QString name = QStringLiteral("roiPaneChip%1").arg(index);
    auto *chip = owner->findChild<QLabel *>(name, Qt::FindDirectChildrenOnly);
    if (chip)
        return chip;
    chip = new QLabel(owner);
    chip->setObjectName(name);
    chip->setAttribute(Qt::WA_TransparentForMouseEvents);
    chip->setStyleSheet("QLabel{background:rgba(0,0,0,140);color:#ffffff;border:1px solid #FFD233;"
                        "border-radius:6px;padding:2px 4px;font-size:11px;font-weight:600;}");
    return chip;
}
// Grid: the ROI as the pane presents it now. While a temporary switch is held
// the pane shows another image with that image's viewport, so the mapping uses
// the presented transform, not the pane's own.
bool gridROIRect(const RawImageView *view, const mviewer::domain::Selection &sel, QWidget *owner,
                 QRect &roi, QRect &bounds)
{
    const QPointF a = view->presentedSourcePointToWidget(QPointF(sel.x, sel.y));
    const QPointF b =
        view->presentedSourcePointToWidget(QPointF(sel.x + sel.width, sel.y + sel.height));
    if (!std::isfinite(a.x()) || !std::isfinite(b.x()))
        return false;
    const QPoint origin = view->mapTo(owner, QPoint(0, 0));
    roi = QRectF(a, b).normalized().translated(origin).toAlignedRect();
    bounds = QRect(origin, view->size());
    return true;
}

void placeROIChip(QLabel *chip, const QString &text, const QRect &roi, const QRect &bounds,
                  int stack)
{
    if (chip->text() != text)
    {
        chip->setText(text);
        chip->adjustSize();
    }
    QSize size = chip->size();
    if (size.isEmpty())
        size = chip->sizeHint();
    size.setWidth(std::min(size.width(), std::max(40, bounds.width() - 8)));
    int x = roi.right() + 6;
    int y = roi.top() + stack * (size.height() + 2);
    if (x + size.width() > bounds.right())
    {
        x = roi.left() + 4;
        y = roi.top() + 4;
    }
    x = std::clamp(x, bounds.left() + 2,
                   std::max(bounds.left() + 2, bounds.right() - size.width()));
    y = std::clamp(y, bounds.top() + 2,
                   std::max(bounds.top() + 2, bounds.bottom() - size.height()));
    const QRect targetGeom(x, y, size.width(), size.height());
    if (chip->geometry() != targetGeom)
        chip->setGeometry(targetGeom);
    if (!chip->isVisible())
        chip->show();
    chip->raise();
}
} // namespace

void CompareWorkspace::updateROISurfaces()
{
    updateROIAvailabilityStatus();
    if (m_copyRoiBtn)
        m_copyRoiBtn->setEnabled(m_roiResult.has_value() && m_roiTable &&
                                 m_roiTable->rowCount() > 0);
    // The aggregated corner HUD is no longer the readout. Per-pane chips sit
    // beside each selection; the full table stays in the analysis panel.
    if (m_roiHud)
    {
        m_roiHud->setVisible(false);
        m_roiHud->setToolTip(tr("Full statistics are in the Inspector panel; R/G, B/G, and H/S/V "
                                "are shown beside each pane's selection."));
    }
    positionROIHud();
}

void CompareWorkspace::positionROIHud()
{
    // Chips follow the image each slot shows (Space / 1-8 hold included): the
    // numbers, the anchor and the visibility all come from displayedImageIndex.
    const int paneCount = std::min(m_engine.imageCount(), static_cast<int>(m_cellViews.size()));
    const bool canvas = m_compareCanvas && m_compareCanvas->isVisible();
    const bool split = canvas && m_splitChk && m_splitChk->isChecked();
    const bool canvasHold = canvas && m_canvasTemporaryPane >= 0;
    for (int index = 0; index < 8; ++index)
    {
        QLabel *chip = roiChip(this, index);
        const int shown = index < paneCount ? displayedImageIndex(index) : -1;
        RawImageView *view = index < paneCount ? m_cellViews[static_cast<size_t>(index)] : nullptr;
        RawImageView *shownView =
            shown >= 0 && shown < paneCount ? m_cellViews[static_cast<size_t>(shown)] : nullptr;
        const mviewer::domain::Selection paneSel =
            shownView ? (m_roiLinked ? m_lastSelection : shownView->selection())
                      : mviewer::domain::Selection{};
        // A full-canvas hold draws one image (labelled as slot 0); split shows two.
        const bool slotShown = canvas ? (split ? index < 2 : (!canvasHold || index == 0)) : true;
        if (paneSel.isEmpty() || !view || !slotShown || (!canvas && !view->isVisible()))
        {
            chip->hide();
            continue;
        }
        QRect roi;
        QRect bounds;
        if (canvas)
        {
            const int geomPane = split ? index : 0;
            const int imagePane = canvasHold ? shown : geomPane;
            const QRectF destination = cellFullDestRect(imagePane, canvasPaneGeometry(geomPane));
            const QRectF mapped =
                mviewer::ui::roiPresentationRect(paneSel, shownView->sourceSize(), destination);
            const QPoint origin = m_compareCanvas->mapTo(this, QPoint(0, 0));
            roi = mapped.translated(origin).toAlignedRect();
            // Split keeps each chip inside its own half, so a held half places
            // its chip exactly where the source half does.
            bounds = split ? canvasPaneGeometry(geomPane).toAlignedRect().translated(origin)
                           : QRect(origin, m_compareCanvas->size());
        }
        else if (!gridROIRect(view, paneSel, this, roi, bounds))
        {
            chip->hide();
            continue;
        }
        placeROIChip(chip, roiChipText(m_roiResult, shown), roi, bounds,
                     canvas && !split ? index : 0);
    }
}
