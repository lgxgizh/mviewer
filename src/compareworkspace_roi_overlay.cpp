#include "compareworkspace_p.h"
#include "widgets/roioverlay.h"

#include <algorithm>
#include <cmath>

namespace
{
QString roiChipText(const std::optional<mviewer::ui::ROIStatsBatchResult> &result, int index)
{
    if (!result || index < 0 || index >= static_cast<int>(result->panes.size()))
        return QStringLiteral("V …");
    const auto &pane = result->panes[static_cast<size_t>(index)];
    if (!pane.stats.valid)
        return QStringLiteral("V —");
    return QStringLiteral("V %1  R %2  G %3  B %4")
        .arg(QString::number(pane.stats.vMean, 'f', 1), QString::number(pane.stats.rMean, 'f', 1),
             QString::number(pane.stats.gMean, 'f', 1), QString::number(pane.stats.bMean, 'f', 1));
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
    chip->setStyleSheet(
        "QLabel{background:rgba(16,16,16,220);color:#ffffff;border:1px solid #FFD233;"
        "border-radius:3px;padding:2px 4px;font-size:11px;font-weight:600;}");
    return chip;
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
        m_roiHud->setToolTip(tr("完整统计在检视面板；各窗格选区旁显示 V 与 RGB 均值"));
    }
    positionROIHud();
}

void CompareWorkspace::positionROIHud()
{
    const int paneCount = m_engine.imageCount();
    const bool show = !m_lastSelection.isEmpty() && m_roiLinked && paneCount >= 1;
    const bool canvas = m_compareCanvas && m_compareCanvas->isVisible();
    const bool split = canvas && m_splitChk && m_splitChk->isChecked();
    for (int index = 0; index < 8; ++index)
    {
        QLabel *chip = roiChip(this, index);
        const bool paneVisible =
            show && index < paneCount && index < m_cellViews.size() && m_cellViews[index] &&
            (canvas ? (split ? index < 2 : true) : m_cellViews[index]->isVisible());
        if (!paneVisible)
        {
            chip->hide();
            continue;
        }
        RawImageView *view = m_cellViews[index];
        QRect roi;
        QRect bounds;
        if (canvas)
        {
            const int geomPane = split ? index : 0;
            const QRectF destination = cellFullDestRect(geomPane, canvasPaneGeometry(geomPane));
            QRectF mapped =
                mviewer::ui::roiPresentationRect(m_lastSelection, view->sourceSize(), destination);
            const QPoint origin = m_compareCanvas->mapTo(this, QPoint(0, 0));
            roi = mapped.translated(origin).toAlignedRect();
            bounds = QRect(origin, m_compareCanvas->size());
        }
        else
        {
            const QPointF a =
                view->sourcePointToWidget(QPointF(m_lastSelection.x, m_lastSelection.y));
            const QPointF b =
                view->sourcePointToWidget(QPointF(m_lastSelection.x + m_lastSelection.width,
                                                  m_lastSelection.y + m_lastSelection.height));
            if (!std::isfinite(a.x()) || !std::isfinite(b.x()))
            {
                chip->hide();
                continue;
            }
            const QPoint origin = view->mapTo(this, QPoint(0, 0));
            roi = QRectF(a, b).normalized().translated(origin).toAlignedRect();
            bounds = QRect(origin, view->size());
        }
        chip->setText(roiChipText(m_roiResult, index));
        chip->adjustSize();
        QSize size = chip->sizeHint();
        size.setWidth(std::min(size.width(), std::max(40, bounds.width() - 8)));
        int x = roi.right() + 6;
        int y = roi.top() + (canvas && !split ? index * (size.height() + 2) : 0);
        if (x + size.width() > bounds.right())
        {
            x = roi.left() + 4;
            y = roi.top() + 4;
        }
        x = std::clamp(x, bounds.left() + 2,
                       std::max(bounds.left() + 2, bounds.right() - size.width()));
        y = std::clamp(y, bounds.top() + 2,
                       std::max(bounds.top() + 2, bounds.bottom() - size.height()));
        chip->setGeometry(x, y, size.width(), size.height());
        chip->show();
        chip->raise();
    }
}
