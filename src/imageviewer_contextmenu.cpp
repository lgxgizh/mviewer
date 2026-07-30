#include "imageviewer.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/Analyzer.h"
#include "core/image/ImageRepository.h"
#include "core/image/QtConvert.h"
#include "core/render/RenderEngine.h"
#include "core/trace/Trace.h"
#include "gpu/GpuTileUploader.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMatrix4x4>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLTextureBlitter>
#include <QPainter>
#include <QPointer>
#include <QRect>
#include <QResizeEvent>
#include <QSettings>
#include <QTimer>
#include <QWheelEvent>
#include <cmath>
#include <cstring>

void ImageViewer::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *aCopy = menu.addAction("复制图片");
    QAction *aCopyPath = menu.addAction("复制路径");
    QAction *aCopyColor = menu.addAction("复制像素颜色 (#RRGGBB)");
    menu.addSeparator();
    QAction *aSaveAs = menu.addAction("另存为...");
    menu.addSeparator();
    QAction *aZoomIn = menu.addAction("放大 (+)");
    QAction *aZoomOut = menu.addAction("缩小 (-)");
    QAction *aZoomFit = menu.addAction("适应窗口 (0)");
    QAction *aZoomActual = menu.addAction("实际大小 (1)");
    menu.addSeparator();
    QAction *aSelectRegion = menu.addAction("框选区域 (R)");
    aSelectRegion->setCheckable(true);
    aSelectRegion->setChecked(m_selectMode);
    menu.addSeparator();

    // F4 (M22): live overlay toggle
    QAction *aOvNone = menu.addAction("无叠加");
    QAction *aOvZebra = menu.addAction("过曝/欠曝斑马线(&Z)");
    QAction *aOvFalse = menu.addAction("伪彩色(&C)");
    aOvNone->setCheckable(true);
    aOvZebra->setCheckable(true);
    aOvFalse->setCheckable(true);
    aOvNone->setChecked(m_overlayMode == mviewer::OverlayMode::None);
    aOvZebra->setChecked(m_overlayMode == mviewer::OverlayMode::Zebra);
    aOvFalse->setChecked(m_overlayMode == mviewer::OverlayMode::FalseColor);

    // A-7.3: "分析" submenu — list every registered analyzer for one-click run.
    QMenu *analyzeMenu = menu.addMenu("分析");
    QList<QAction *> analyzeActions;
    if (m_frame)
    {
        const auto ids = AnalyzerRegistry::instance().availableAnalyzers();
        for (const auto &id : ids)
        {
            const auto info = AnalyzerRegistry::instance().infoFor(id);
            const QString label =
                info ? QString::fromStdString(info->name) : QString::fromStdString(id);
            QAction *a = analyzeMenu->addAction(label);
            a->setData(QString::fromStdString(id));
            analyzeActions.append(a);
        }
        if (analyzeActions.isEmpty())
            analyzeMenu->addAction("（无可用分析器）")->setEnabled(false);
    }
    else
    {
        analyzeMenu->addAction("（请先打开图片）")->setEnabled(false);
    }
    menu.addSeparator();
    QAction *aNext = menu.addAction("下一张 (→)");
    QAction *aPrev = menu.addAction("上一张 (←)");
    menu.addSeparator();
    QAction *aFullscreen = menu.addAction("全屏 (F)");
    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen)
        return;
    // A-7.3: route analyzer selection through AnalysisPanel (unified entry).
    // MainWindow shows the panel and runs the analyzer so results land in the
    // Plugin tab — not a one-off QMessageBox.
    if (analyzeActions.contains(chosen) && m_frame)
    {
        emit analysisRequested(chosen->data().toString());
        return;
    }
    if (chosen == aCopy)
        QApplication::clipboard()->setPixmap(m_pixmap);
    else if (chosen == aCopyPath)
        QApplication::clipboard()->setText(m_currentPath);
    else if (chosen == aCopyColor)
    {
        // Read pixel color at the cursor position (event->pos() in widget coords).
        const QPoint pos = event->pos();
        if (!m_pixmap.isNull() && m_frame)
        {
            const int iw = m_pixmap.width();
            const int ih = m_pixmap.height();
            const int ix = static_cast<int>((pos.x() - m_view.offsetX) / m_view.scale);
            const int iy = static_cast<int>((pos.y() - m_view.offsetY) / m_view.scale);
            if (ix >= 0 && ix < iw && iy >= 0 && iy < ih)
            {
                const ImageBuffer view = m_frame->pixels().view();
                if (view.channelsPerPixel() >= 3)
                {
                    const uint8_t *p = view.data + static_cast<size_t>(iy) * view.stride() +
                                       static_cast<size_t>(ix) * view.channelsPerPixel();
                    QApplication::clipboard()->setText(QString("#%1%2%3")
                                                           .arg(p[0], 2, 16, QChar('0'))
                                                           .arg(p[1], 2, 16, QChar('0'))
                                                           .arg(p[2], 2, 16, QChar('0')));
                }
            }
        }
    }
    else if (chosen == aSaveAs)
    {
        if (!m_pixmap.isNull())
        {
            const QString defaultName = QFileInfo(m_currentPath).completeBaseName() + "_copy.png";
            const QString path = QFileDialog::getSaveFileName(
                this, "另存为", defaultName,
                "PNG (*.png);;JPEG (*.jpg);;BMP (*.bmp);;WebP (*.webp)");
            if (!path.isEmpty())
                m_pixmap.save(path);
        }
    }
    else if (chosen == aZoomIn)
        zoomIn();
    else if (chosen == aZoomOut)
        zoomOut();
    else if (chosen == aZoomFit)
        zoomFit();
    else if (chosen == aZoomActual)
        zoomActual();
    else if (chosen == aSelectRegion)
        setSelectMode(!m_selectMode);
    else if (chosen == aNext)
        emit requestNext();
    else if (chosen == aOvNone)
        setOverlayMode(mviewer::OverlayMode::None);
    else if (chosen == aOvZebra)
        setOverlayMode(mviewer::OverlayMode::Zebra);
    else if (chosen == aOvFalse)
        setOverlayMode(mviewer::OverlayMode::FalseColor);
    else if (chosen == aPrev)
        emit requestPrev();
    else if (chosen == aFullscreen)
        isFullScreen() ? showNormal() : showFullScreen();
}
