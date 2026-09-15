#include "imageviewer.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/analysis/PixelInspector.h"
#include "core/analyzer/Analyzer.h"
#include "core/image/ImageLoadingFacade.h"
#include "core/image/QtConvert.h"
#include "core/render/RenderEngine.h"
#include "core/trace/Trace.h"
#include "gpu/GpuTileUploader.h"
#include "thumbnailprovider.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
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
#include <QSaveFile>
#include <QSettings>
#include <QTimer>
#include <QTransform>
#include <QWheelEvent>
#include <cmath>
#include <cstring>

namespace
{
void addFrameContextActions(QMenu &menu, bool animated, bool playing, int frameIndex,
                            int frameCount, QAction *&play, QAction *&restart, QAction *&previous,
                            QAction *&next)
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

void addOverlayContextActions(QMenu &menu, mviewer::OverlayMode mode, QAction *&none,
                              QAction *&zebra, QAction *&falseColor, QAction *&channelR,
                              QAction *&channelG, QAction *&channelB, QAction *&channelY,
                              QAction *&channelV)
{
    none = menu.addAction("无叠加 (Shift+1)");
    zebra = menu.addAction("过曝/欠曝斑马线(&Z)");
    falseColor = menu.addAction("伪彩色(&C)");
    channelR = menu.addAction("R 通道 (Shift+2)");
    channelG = menu.addAction("G 通道 (Shift+3)");
    channelB = menu.addAction("B 通道 (Shift+4)");
    channelY = menu.addAction("Y 亮度 (Shift+5)");
    channelV = menu.addAction("V 明度 (Shift+6)");
    for (QAction *a : {none, zebra, falseColor, channelR, channelG, channelB, channelY, channelV})
        a->setCheckable(true);
    none->setChecked(mode == mviewer::OverlayMode::None);
    zebra->setChecked(mode == mviewer::OverlayMode::Zebra);
    falseColor->setChecked(mode == mviewer::OverlayMode::FalseColor);
    channelR->setChecked(mode == mviewer::OverlayMode::ChannelR);
    channelG->setChecked(mode == mviewer::OverlayMode::ChannelG);
    channelB->setChecked(mode == mviewer::OverlayMode::ChannelB);
    channelY->setChecked(mode == mviewer::OverlayMode::ChannelY);
    channelV->setChecked(mode == mviewer::OverlayMode::ChannelV);
}

void setContextImageActionAvailability(QAction *copy, QAction *copyPath, QMenu *copyColorMenu,
                                       QAction *saveAs, QAction *zoomIn, QAction *zoomOut,
                                       QAction *zoomFit, QAction *zoomActual, QMenu *zoomPresetsMenu,
                                       QAction *selectRegion, bool hasPath, bool hasFrame,
                                       bool hasDisplay)
{
    copy->setEnabled(hasPath);
    copyPath->setEnabled(hasPath);
    if (copyColorMenu)
        copyColorMenu->menuAction()->setEnabled(hasFrame);
    saveAs->setEnabled(hasFrame);
    zoomIn->setEnabled(hasDisplay);
    zoomOut->setEnabled(hasDisplay);
    zoomFit->setEnabled(hasDisplay);
    zoomActual->setEnabled(hasDisplay);
    if (zoomPresetsMenu)
        zoomPresetsMenu->menuAction()->setEnabled(hasDisplay);
    selectRegion->setEnabled(hasDisplay);
}

void addCopyContextActions(QMenu &menu, QAction *&copy, QAction *&copyPath, QMenu *&colorMenu,
                           QAction *&copyHex, QAction *&copyRgb, QAction *&copyFloat,
                           QAction *&copyHsv)
{
    copy = menu.addAction("复制图片 (Ctrl+C)");
    copyPath = menu.addAction("复制路径 (Ctrl+Shift+C)");
    colorMenu = menu.addMenu("复制像素值");
    copyHex = colorMenu->addAction("十六进制 (#RRGGBB) (Shift+C)");
    copyRgb = colorMenu->addAction("RGB 值 RGB(r, g, b)");
    copyFloat = colorMenu->addAction("归一化浮点 (0.xxx, 0.yyy, 0.zzz)");
    copyHsv = colorMenu->addAction("HSV 值 HSV(h°, s%, v%)");
}

void copyPixelValue(const PixelRGBA &px, int format)
{
    if (!px.valid)
        return;
    QString text;
    switch (format)
    {
    case 0:
        text = QString("#%1%2%3")
                   .arg(px.r, 2, 16, QChar('0'))
                   .arg(px.g, 2, 16, QChar('0'))
                   .arg(px.b, 2, 16, QChar('0'))
                   .toUpper();
        break;
    case 1:
        text = QString("RGB(%1, %2, %3)").arg(px.r).arg(px.g).arg(px.b);
        break;
    case 2:
        text = QString("(%1, %2, %3)")
                   .arg(px.r / 255.0, 0, 'f', 4)
                   .arg(px.g / 255.0, 0, 'f', 4)
                   .arg(px.b / 255.0, 0, 'f', 4);
        break;
    case 3:
    {
        const auto hsv =
            mviewer::core::toColorSpace(static_cast<uint8_t>(px.r), static_cast<uint8_t>(px.g),
                                        static_cast<uint8_t>(px.b), mviewer::core::ColorSpace::HSV);
        text = QString("HSV(%1°, %2%, %3%)")
                   .arg(std::round(hsv.c1))
                   .arg(std::round(hsv.c2))
                   .arg(std::round(hsv.c3));
        break;
    }
    default:
        break;
    }
    if (!text.isEmpty())
        QApplication::clipboard()->setText(text);
}

void populateAnalyzeSubmenu(QMenu &menu, const std::shared_ptr<ImageFrame> &frame,
                            QList<QAction *> &analyzeActions)
{
    QMenu *analyzeMenu = menu.addMenu("分析");
    if (frame)
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
}
} // namespace

void ImageViewer::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *aCopy = nullptr;
    QAction *aCopyPath = nullptr;
    QMenu *mCopyColor = nullptr;
    QAction *aCopyHex = nullptr;
    QAction *aCopyRgb = nullptr;
    QAction *aCopyFloat = nullptr;
    QAction *aCopyHsv = nullptr;
    addCopyContextActions(menu, aCopy, aCopyPath, mCopyColor, aCopyHex, aCopyRgb, aCopyFloat,
                          aCopyHsv);
    menu.addSeparator();
    QAction *aSaveAs = menu.addAction("另存为...");
    QAction *aRotateCW = menu.addAction("顺时针旋转 90° (Ctrl+R)");
    QAction *aRotateCCW = menu.addAction("逆时针旋转 90° (Ctrl+Shift+R)");
    QAction *aFlipH = menu.addAction("水平翻转 (H)");
    QAction *aFlipV = menu.addAction("垂直翻转 (V)");
    aRotateCW->setEnabled(!m_currentPath.isEmpty());
    aRotateCCW->setEnabled(!m_currentPath.isEmpty());
    aFlipH->setEnabled(!m_currentPath.isEmpty());
    aFlipV->setEnabled(!m_currentPath.isEmpty());
    QAction *aPlay = nullptr;
    QAction *aRestart = nullptr;
    QAction *aPrevFrame = nullptr;
    QAction *aNextFrame = nullptr;
    if (isMultiFrame())
    {
        menu.addSeparator();
        addFrameContextActions(menu, m_sequence.animated, isPlaying(), m_frameIndex, frameCount(),
                               aPlay, aRestart, aPrevFrame, aNextFrame);
    }
    menu.addSeparator();
    QAction *aZoomIn = menu.addAction("放大 (+)");
    QAction *aZoomOut = menu.addAction("缩小 (-)");
    QAction *aZoomFit = menu.addAction("适应窗口 (0)");
    QAction *aZoomActual = menu.addAction("实际大小 (1)");
    QMenu *mZoomPresets = menu.addMenu("缩放预设");
    mZoomPresets->addAction("50%")->setData(0.5);
    mZoomPresets->addAction("100% (实际大小)")->setData(1.0);
    mZoomPresets->addAction("200%")->setData(2.0);
    mZoomPresets->addAction("400%")->setData(4.0);
    mZoomPresets->addAction("800% (像素网格)")->setData(8.0);
    menu.addSeparator();
    QAction *aSelectRegion = menu.addAction("框选区域 (R)");
    aSelectRegion->setCheckable(true);
    aSelectRegion->setChecked(m_selectMode);
    setContextImageActionAvailability(
        aCopy, aCopyPath, mCopyColor, aSaveAs, aZoomIn, aZoomOut, aZoomFit, aZoomActual,
        mZoomPresets, aSelectRegion, !m_currentPath.isEmpty(), m_frame && m_frame->isValid(),
        hasDisplayImage());
    menu.addSeparator();
    QAction *aOvNone = nullptr, *aOvZebra = nullptr, *aOvFalse = nullptr;
    QAction *aOvR = nullptr, *aOvG = nullptr, *aOvB = nullptr, *aOvY = nullptr, *aOvV = nullptr;
    addOverlayContextActions(menu, m_overlayMode, aOvNone, aOvZebra, aOvFalse, aOvR, aOvG, aOvB,
                             aOvY, aOvV);

    // A-7.3: "分析" submenu — list every registered analyzer for one-click run.
    QList<QAction *> analyzeActions;
    populateAnalyzeSubmenu(menu, m_frame, analyzeActions);
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
    if (handleContextTransformAction(chosen, aRotateCW, aRotateCCW, aFlipH, aFlipV) ||
        handleContextCopyAction(chosen, aCopy, aCopyPath, aCopyHex, aCopyRgb, aCopyFloat, aCopyHsv,
                                event) ||
        handleContextImageAction(chosen, aSaveAs, aZoomIn, aZoomOut, aZoomFit, aZoomActual,
                                 aSelectRegion))
        return;
    handleContextNavigationAction(chosen, aNext, aPrev, aOvNone, aOvZebra, aOvFalse, aOvR, aOvG,
                                  aOvB, aOvY, aFullscreen, aOvV);
}

bool ImageViewer::handleContextCopyAction(QAction *chosen, QAction *copy, QAction *copyPath,
                                          QAction *copyHex, QAction *copyRgb, QAction *copyFloat,
                                          QAction *copyHsv, QContextMenuEvent *event)
{
    if (chosen == copy)
    {
        copyToClipboard();
        return true;
    }
    if (chosen == copyPath)
    {
        QApplication::clipboard()->setText(m_currentPath);
        return true;
    }
    int format = -1;
    if (chosen == copyHex)
        format = 0;
    else if (chosen == copyRgb)
        format = 1;
    else if (chosen == copyFloat)
        format = 2;
    else if (chosen == copyHsv)
        format = 3;

    if (format >= 0)
    {
        PixelRGBA px{};
        const QPoint pos = event->pos();
        if (m_frame && m_frame->isValid())
        {
            const int ix = static_cast<int>((pos.x() - m_view.offsetX) / m_view.scale);
            const int iy = static_cast<int>((pos.y() - m_view.offsetY) / m_view.scale);
            if (ix >= 0 && ix < m_frame->width() && iy >= 0 && iy < m_frame->height())
                px = samplePixel(m_frame->pixels(), ix, iy);
        }
        if (!px.valid)
            px = m_lastHoverPixel;
        copyPixelValue(px, format);
        return true;
    }
    return false;
}

bool ImageViewer::handleContextTransformAction(QAction *chosen, QAction *rotateCWAct,
                                               QAction *rotateCCWAct, QAction *flipHAct,
                                               QAction *flipVAct)
{
    if (chosen == rotateCWAct)
        return rotateCW();
    if (chosen == rotateCCWAct)
        return rotateCCW();
    if (chosen == flipHAct)
        return flipHorizontal();
    if (chosen == flipVAct)
        return flipVertical();
    return false;
}

bool ImageViewer::handleTransformKey(int key, Qt::KeyboardModifiers modifiers)
{
    const bool plain = (modifiers == Qt::NoModifier);
    const bool ctrl = (modifiers == Qt::ControlModifier);
    const bool shift = (modifiers == Qt::ShiftModifier);
    const bool shiftCtrl = (modifiers == (Qt::ControlModifier | Qt::ShiftModifier));
    if (ctrl && key == Qt::Key_R)
        return rotateCW();
    if (shiftCtrl && key == Qt::Key_R)
        return rotateCCW();
    if ((plain || shiftCtrl) && key == Qt::Key_H)
        return flipHorizontal();
    if ((plain || shiftCtrl) && key == Qt::Key_V)
        return flipVertical();
    if (shift && key == Qt::Key_C)
    {
        PixelRGBA px = m_lastHoverPixel;
        if (!px.valid && m_frame && m_frame->isValid())
        {
            const QPoint pos = mapFromGlobal(QCursor::pos());
            const int ix = static_cast<int>((pos.x() - m_view.offsetX) / m_view.scale);
            const int iy = static_cast<int>((pos.y() - m_view.offsetY) / m_view.scale);
            if (ix >= 0 && ix < m_frame->width() && iy >= 0 && iy < m_frame->height())
                px = samplePixel(m_frame->pixels(), ix, iy);
        }
        if (px.valid)
        {
            copyPixelValue(px, 0);
            return true;
        }
    }
    if (ctrl && key == Qt::Key_C)
    {
        copyToClipboard();
        return true;
    }
    if (shiftCtrl && key == Qt::Key_C)
    {
        if (!m_currentPath.isEmpty())
        {
            QApplication::clipboard()->setText(m_currentPath);
            return true;
        }
    }
    return false;
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
    else if (chosen && chosen->data().isValid() && chosen->data().userType() == QMetaType::Double)
        zoomTo(chosen->data().toDouble());
    else if (chosen == selectRegion)
        setSelectMode(!m_selectMode);
    else
        return false;
    return true;
}

bool ImageViewer::handleContextNavigationAction(QAction *chosen, QAction *next, QAction *prev,
                                                QAction *overlayNone, QAction *overlayZebra,
                                                QAction *overlayFalse, QAction *overlayR,
                                                QAction *overlayG, QAction *overlayB,
                                                QAction *overlayY, QAction *fullscreen,
                                                QAction *overlayV)
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
    else if (chosen == overlayR)
        setOverlayMode(mviewer::OverlayMode::ChannelR);
    else if (chosen == overlayG)
        setOverlayMode(mviewer::OverlayMode::ChannelG);
    else if (chosen == overlayB)
        setOverlayMode(mviewer::OverlayMode::ChannelB);
    else if (chosen == overlayY)
        setOverlayMode(mviewer::OverlayMode::ChannelY);
    else if (chosen == overlayV)
        setOverlayMode(mviewer::OverlayMode::ChannelV);
    else if (chosen == fullscreen)
        toggleFullscreen();
    else
        return false;
    return true;
}

bool ImageViewer::rotateCW()
{
    return rotateImage(90);
}

bool ImageViewer::rotateCCW()
{
    return rotateImage(-90);
}

bool ImageViewer::rotateImage(int angle)
{
    if (m_currentPath.isEmpty())
        return false;

    int normAngle = angle % 360;
    if (normAngle < 0)
        normAngle += 360;
    if (normAngle == 0)
        return true;

    QImageReader reader(m_currentPath);
    reader.setAutoTransform(true);
    const QImage original = reader.read();
    if (original.isNull())
    {
        QMessageBox::warning(this, tr("旋转失败"), tr("无法读取图片：%1").arg(m_currentPath));
        return false;
    }

    QTransform transform;
    transform.rotate(normAngle);
    const QImage rotated = original.transformed(transform, Qt::SmoothTransformation);
    if (rotated.isNull())
        return false;

    QByteArray format = reader.format();
    if (format.isEmpty())
        format = QFileInfo(m_currentPath).suffix().toLatin1();

    QSaveFile saveFile(m_currentPath);
    if (!saveFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::warning(this, tr("旋转失败"), tr("无法写入文件：%1").arg(saveFile.errorString()));
        return false;
    }

    int quality = 95;
    const QString fmtLower = QString::fromLatin1(format).toLower();
    if (fmtLower == "png" || fmtLower == "bmp")
        quality = -1;

    if (!rotated.save(&saveFile, format.constData(), quality) || !saveFile.commit())
    {
        saveFile.cancelWriting();
        QMessageBox::warning(this, tr("旋转失败"), tr("保存文件失败：%1").arg(m_currentPath));
        return false;
    }

    mviewer::core::ImageLoadingFacade::instance().invalidateSource(
        m_currentPath.toUtf8().toStdString());
    ThumbnailProvider::invalidateSource(m_currentPath.toUtf8().toStdString());

    emit fileRotated(m_currentPath);
    refreshSource(m_currentPath);
    return true;
}

bool ImageViewer::flipHorizontal()
{
    return flipImage(true);
}

bool ImageViewer::flipVertical()
{
    return flipImage(false);
}

bool ImageViewer::flipImage(bool horizontal)
{
    if (m_currentPath.isEmpty())
        return false;

    QImageReader reader(m_currentPath);
    reader.setAutoTransform(true);
    const QImage original = reader.read();
    if (original.isNull())
    {
        QMessageBox::warning(this, tr("翻转失败"), tr("无法读取图片：%1").arg(m_currentPath));
        return false;
    }

    const QImage flipped = original.mirrored(horizontal, !horizontal);
    if (flipped.isNull())
        return false;

    QByteArray format = reader.format();
    if (format.isEmpty())
        format = QFileInfo(m_currentPath).suffix().toLatin1();

    QSaveFile saveFile(m_currentPath);
    if (!saveFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::warning(this, tr("翻转失败"), tr("无法写入文件：%1").arg(saveFile.errorString()));
        return false;
    }

    int quality = 95;
    const QString fmtLower = QString::fromLatin1(format).toLower();
    if (fmtLower == "png" || fmtLower == "bmp")
        quality = -1;

    if (!flipped.save(&saveFile, format.constData(), quality) || !saveFile.commit())
    {
        saveFile.cancelWriting();
        QMessageBox::warning(this, tr("翻转失败"), tr("保存文件失败：%1").arg(m_currentPath));
        return false;
    }

    mviewer::core::ImageLoadingFacade::instance().invalidateSource(
        m_currentPath.toUtf8().toStdString());
    ThumbnailProvider::invalidateSource(m_currentPath.toUtf8().toStdString());

    emit fileRotated(m_currentPath);
    refreshSource(m_currentPath);
    return true;
}

void ImageViewer::zoomTo(double targetScale)
{
    if (!hasDisplayImage() || targetScale <= 0.0)
        return;
    m_view.screenW = width();
    m_view.screenH = height();
    m_view.zoomAt(width() / 2.0, height() / 2.0, targetScale / m_view.scale);
    advanceViewportRevision();
    m_fitMode = false;
    emitZoom();
    update();
}
