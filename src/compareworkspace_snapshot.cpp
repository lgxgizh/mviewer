#include "compareworkspace_p.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace
{

struct SnapshotAnnotations
{
    QString modeText;
    int zoomPercent = 100;
    QString compDetail;
    QString metricsText;
    QString rightText;
};

QString snapshotDirectory()
{
    QSettings settings;
    const QString saved = settings.value(QStringLiteral("compare/snapshotDir")).toString();
    if (!saved.isEmpty() && QDir(saved).exists())
        return saved;
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (!pictures.isEmpty())
        return pictures;
    return QDir::homePath();
}

QString snapshotSuggestion()
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString name = QStringLiteral("compare_") + stamp + QStringLiteral(".png");
    return QDir(snapshotDirectory()).filePath(name);
}

void paintSnapshotBanner(QPainter &p, const SnapshotAnnotations &a, int w, int h, int bannerHeight)
{
    const QRect bannerRect(0, h, w, bannerHeight);
    p.fillRect(bannerRect, QColor(24, 24, 28));
    p.setPen(QColor(52, 53, 65));
    p.drawLine(0, h, w, h);

    QFont font = QApplication::font();
    font.setPointSize(9);
    p.setFont(font);
    const QFontMetrics fm(font);

    const int rightWidth = fm.horizontalAdvance(a.rightText);
    const int rightX = w - rightWidth - 12;
    if (rightX > 0)
    {
        p.setPen(QColor(140, 142, 158));
        p.drawText(QRect(rightX, h, rightWidth, bannerHeight), Qt::AlignVCenter | Qt::AlignRight,
                   a.rightText);
    }

    int curX = 12;
    const int maxLeftX = std::max(curX, rightX - 16);

    const int modeBadgeW = fm.horizontalAdvance(a.modeText) + 12;
    const int badgeH = 20;
    const int badgeY = h + (bannerHeight - badgeH) / 2;
    if (curX + modeBadgeW < maxLeftX)
    {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(42, 44, 58));
        p.drawRoundedRect(QRect(curX, badgeY, modeBadgeW, badgeH), 3, 3);
        p.setPen(QColor(90, 160, 245));
        p.drawText(QRect(curX, badgeY, modeBadgeW, badgeH), Qt::AlignCenter, a.modeText);
        curX += modeBadgeW + 8;
    }

    const QString zoomStr = QStringLiteral("%1%").arg(a.zoomPercent);
    const int zoomBadgeW = fm.horizontalAdvance(zoomStr) + 10;
    if (curX + zoomBadgeW < maxLeftX)
    {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(36, 38, 48));
        p.drawRoundedRect(QRect(curX, badgeY, zoomBadgeW, badgeH), 3, 3);
        p.setPen(QColor(180, 185, 200));
        p.drawText(QRect(curX, badgeY, zoomBadgeW, badgeH), Qt::AlignCenter, zoomStr);
        curX += zoomBadgeW + 12;
    }

    QString leftSummary;
    if (!a.compDetail.isEmpty() && !a.metricsText.isEmpty())
        leftSummary = a.compDetail + QStringLiteral("  |  ") + a.metricsText;
    else if (!a.compDetail.isEmpty())
        leftSummary = a.compDetail;
    else if (!a.metricsText.isEmpty())
        leftSummary = a.metricsText;

    if (!leftSummary.isEmpty() && curX < maxLeftX)
    {
        const int availW = maxLeftX - curX;
        const QString elided = fm.elidedText(leftSummary, Qt::ElideRight, availW);
        p.setPen(QColor(215, 218, 230));
        p.drawText(QRect(curX, h, availW, bannerHeight), Qt::AlignVCenter | Qt::AlignLeft, elided);
    }
}

QString snapshotModeText(const QCheckBox *split, const QCheckBox *swipe, const QCheckBox *overlay,
                         const QCheckBox *checker, const QCheckBox *diff, const QCheckBox *blink)
{
    if (split && split->isChecked())
        return QObject::tr("左右分割");
    if (swipe && swipe->isChecked())
        return QObject::tr("卷帘对比");
    if (overlay && overlay->isChecked())
        return QObject::tr("叠加对比");
    if (checker && checker->isChecked())
        return QObject::tr("棋盘格");
    if (diff && diff->isChecked())
        return QObject::tr("差异高亮");
    if (blink && blink->isChecked())
        return QObject::tr("闪烁对比");
    return QObject::tr("网格对比");
}

int snapshotZoomPercent(const QList<RawImageView *> &views, double sharedZoom)
{
    if (!views.isEmpty() && views[0])
        return static_cast<int>(std::round(views[0]->scale() * 100.0));
    if (sharedZoom > 0.0)
        return static_cast<int>(std::round(sharedZoom * 100.0));
    return 100;
}

QString snapshotComparedDetail(const QStringList &imgs)
{
    if (imgs.size() == 2)
    {
        return QStringLiteral("A: %1  vs  B: %2")
            .arg(QFileInfo(imgs[0]).fileName(), QFileInfo(imgs[1]).fileName());
    }
    if (imgs.size() > 2)
        return QObject::tr("%1 张图片对比").arg(imgs.size());
    return {};
}

} // namespace

QPixmap CompareWorkspace::grabComparisonView()
{
    QWidget *target = nullptr;
    if (m_compareCanvas && m_compareCanvas->isVisible())
        target = m_compareCanvas;
    else
        target = m_compareGridPage;
    if (!target)
        target = this;
    const QPixmap captured = target->grab();
    if (captured.isNull())
        return captured;

    SnapshotAnnotations a;
    a.modeText = snapshotModeText(m_splitChk, m_swipeChk, m_overlayChk, m_checkerChk,
                                  m_diffHighlightChk, m_blinkChk);
    a.zoomPercent = snapshotZoomPercent(m_cellViews, m_sharedZoomRatio);
    a.compDetail = snapshotComparedDetail(comparedImages());
    if (!m_lastSelection.isEmpty())
    {
        a.compDetail += QString("  [ROI: %1,%2 %3×%4]")
                            .arg(m_lastSelection.x)
                            .arg(m_lastSelection.y)
                            .arg(m_lastSelection.width)
                            .arg(m_lastSelection.height);
    }
    const bool hasAdjust = std::any_of(m_cellAdjusts.cbegin(), m_cellAdjusts.cend(),
                                       [](const CellAdjust &adj) { return !adj.isIdentity(); });
    if (hasAdjust)
        a.compDetail += QStringLiteral("  [已调参]");

    if (m_metricLabel && !m_metricLabel->text().isEmpty())
    {
        a.metricsText = m_metricLabel->text().trimmed();
        a.metricsText.replace(QLatin1Char('\n'), QStringLiteral("  "));
    }

    a.rightText = QStringLiteral("MViewer · ") +
                  QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));

    const int bannerHeight = 36;
    const int w = captured.width();
    const int h = captured.height();

    QPixmap result(w, h + bannerHeight);
    result.fill(QColor(24, 24, 28));

    QPainter p(&result);
    p.drawPixmap(0, 0, captured);
    paintSnapshotBanner(p, a, w, h, bannerHeight);
    p.end();

    return result;
}

void CompareWorkspace::copyComparisonViewToClipboard()
{
    const QPixmap pm = grabComparisonView();
    if (pm.isNull())
        return;
    QApplication::clipboard()->setPixmap(pm);
    showCompareStatus(tr("已将当前对比视图复制到剪贴板"));
}

bool CompareWorkspace::saveComparisonViewTo(const QString &path)
{
    if (path.isEmpty())
        return false;
    const QPixmap pm = grabComparisonView();
    if (pm.isNull())
        return false;
    return pm.save(path, "PNG");
}

void CompareWorkspace::saveComparisonViewToFile()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("保存比较截图"),
                                                      snapshotSuggestion(), tr("PNG 图片 (*.png)"));
    if (path.isEmpty())
        return;
    const QString native = QDir::toNativeSeparators(path);
    if (!saveComparisonViewTo(path))
    {
        showCompareStatus(tr("保存截图失败：%1").arg(native));
        return;
    }
    QSettings().setValue(QStringLiteral("compare/snapshotDir"), QFileInfo(path).absolutePath());
    showCompareStatus(tr("已保存截图：%1").arg(native));
}

bool CompareWorkspace::handleClipboardCompareKey(QKeyEvent *event)
{
    const int key = event->key();
    const auto mods = event->modifiers();
    const bool ctrl = (mods == Qt::ControlModifier);

    // Ctrl+Shift+C copies the focused image path. Exact Ctrl+C copies the view.
    if (key == Qt::Key_C && mods == (Qt::ControlModifier | Qt::ShiftModifier))
    {
        const QString path = focusImagePath();
        if (!path.isEmpty())
        {
            const QString native = QDir::toNativeSeparators(path);
            QApplication::clipboard()->setText(native);
            showCompareStatus(tr("已复制路径: %1").arg(native));
        }
        else
        {
            showCompareStatus(tr("没有可复制的路径"));
        }
        event->accept();
        return true;
    }
    if (ctrl && key == Qt::Key_C)
    {
        copyComparisonViewToClipboard();
        event->accept();
        return true;
    }
    if (ctrl && key == Qt::Key_S)
    {
        saveComparisonViewToFile();
        event->accept();
        return true;
    }
    return false;
}
