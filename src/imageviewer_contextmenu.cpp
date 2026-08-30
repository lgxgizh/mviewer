#include "imageviewer.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/Analyzer.h"
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

namespace
{
void addFrameContextActions(QMenu &menu, bool animated, bool playing, int frameIndex,
                            int frameCount, QAction *&play, QAction *&restart,
                            QAction *&previous, QAction *&next)
{
    if (animated)
    {
        play = menu.addAction(playing ? "暂停" : "播放");
        restart = menu.addAction("重新开始");
        previous = menu.addAction("上一帧 (,)");
        next = menu.addAction("下一帧 (.)");
        return;
    }

    restart = menu.addAction("第一页");
    previous = menu.addAction("上一页 (,)");
    next = menu.addAction("下一页 (.)");
    restart->setEnabled(frameIndex > 0);
    previous->setEnabled(frameIndex > 0);
    next->setEnabled(frameIndex + 1 < frameCount);
}
} // namespace

void ImageViewer::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *aCopy = menu.addAction("复制图片");
    QAction *aCopyPath = menu.addAction("复制路径");
    QAction *aCopyColor = menu.addAction("复制像素颜色 (#RRGGBB)");
    menu.addSeparator();
    QAction *aSaveAs = menu.addAction("另存为...");
    QAction *aPlay = nullptr;
    QAction *aRestart = nullptr;
    QAction *aPrevFrame = nullptr;
    QAction *aNextFrame = nullptr;
    if (isMultiFrame())
    {
        menu.addSeparator();
        addFrameContextActions(menu, m_sequence.animated, isPlaying(), m_frameIndex,
                               frameCount(), aPlay, aRestart, aPrevFrame, aNextFrame);
    }
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
    const bool hasBrowsePosition = m_currentIndex >= 0 && m_currentIndex < m_fileList.size();
    aNext->setEnabled(hasBrowsePosition && m_currentIndex + 1 < m_fileList.size());
    aPrev->setEnabled(hasBrowsePosition && m_currentIndex > 0);
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
    if (chosen == aPlay)
    {
        if (isPlaying())
            pause();
        else
            play();
        return;
    }
    if (chosen == aRestart)
    {
        restart();
        return;
    }
    if (chosen == aPrevFrame)
    {
        previousFrame();
        return;
    }
    if (chosen == aNextFrame)
    {
        nextFrame();
        return;
    }
    if (handleContextCopyAction(chosen, aCopy, aCopyPath, aCopyColor, event) ||
        handleContextImageAction(chosen, aSaveAs, aZoomIn, aZoomOut, aZoomFit, aZoomActual,
                                  aSelectRegion))
        return;
    handleContextNavigationAction(chosen, aNext, aPrev, aOvNone, aOvZebra, aOvFalse,
                                  aFullscreen);
}

bool ImageViewer::handleContextCopyAction(QAction *chosen, QAction *copy, QAction *copyPath,
                                          QAction *copyColor, QContextMenuEvent *event)
{
    if (chosen == copy)
        copyToClipboard();
    else if (chosen == copyPath)
        QApplication::clipboard()->setText(m_currentPath);
    else if (chosen == copyColor)
    {
        const QPoint pos = event->pos();
        if (!m_frame || !m_frame->isValid())
            return true;
        const int ix = static_cast<int>((pos.x() - m_view.offsetX) / m_view.scale);
        const int iy = static_cast<int>((pos.y() - m_view.offsetY) / m_view.scale);
        if (ix < 0 || ix >= m_frame->width() || iy < 0 || iy >= m_frame->height())
            return true;
        const PixelRGBA px = samplePixel(m_frame->pixels(), ix, iy);
        if (px.valid)
            QApplication::clipboard()->setText(
                QString("#%1%2%3")
                    .arg(px.r, 2, 16, QChar('0'))
                    .arg(px.g, 2, 16, QChar('0'))
                    .arg(px.b, 2, 16, QChar('0')));
    }
    else
        return false;
    return true;
}

bool ImageViewer::handleContextImageAction(QAction *chosen, QAction *saveAs, QAction *zoomInAction,
                                           QAction *zoomOutAction, QAction *zoomFitAction,
                                           QAction *zoomActualAction, QAction *selectRegion)
{
    if (chosen == saveAs)
    {
        if (m_frame && m_frame->isValid())
        {
            const QString defaultName = QFileInfo(m_currentPath).completeBaseName() + "_copy.png";
            const QString path = QFileDialog::getSaveFileName(
                this, isMultiFrame() ? "另存为当前帧/页" : "另存为", defaultName,
                "PNG (*.png);;JPEG (*.jpg);;BMP (*.bmp);;WebP (*.webp)");
            if (!path.isEmpty())
                saveToPath(path);
        }
    }
    else if (chosen == zoomInAction)
        zoomIn();
    else if (chosen == zoomOutAction)
        zoomOut();
    else if (chosen == zoomFitAction)
        zoomFit();
    else if (chosen == zoomActualAction)
        zoomActual();
    else if (chosen == selectRegion)
        setSelectMode(!m_selectMode);
    else
        return false;
    return true;
}

bool ImageViewer::handleContextNavigationAction(QAction *chosen, QAction *next, QAction *prev,
                                                QAction *overlayNone, QAction *overlayZebra,
                                                QAction *overlayFalse, QAction *fullscreen)
{
    if (chosen == next)
        emit requestNext();
    else if (chosen == prev)
        emit requestPrev();
    else if (chosen == overlayNone)
        setOverlayMode(mviewer::OverlayMode::None);
    else if (chosen == overlayZebra)
        setOverlayMode(mviewer::OverlayMode::Zebra);
    else if (chosen == overlayFalse)
        setOverlayMode(mviewer::OverlayMode::FalseColor);
    else if (chosen == fullscreen)
        toggleFullscreen();
    else
        return false;
    return true;
}
