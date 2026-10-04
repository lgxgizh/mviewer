#include "imageviewer.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/analysis/PixelInspector.h"
#include "core/analyzer/Analyzer.h"
#include "core/image/QtConvert.h"
#include "core/render/RenderEngine.h"
#include "core/trace/Trace.h"
#include "gpu/GpuTileUploader.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMatrix4x4>
#include <QMenu>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLTextureBlitter>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QRect>
#include <QResizeEvent>
#include <QSettings>
#include <QTimer>
#include <QWheelEvent>
#include <cmath>
#include <cstring>
#include <string>

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

void addZoomAndSelectContextActions(QMenu &menu, bool lockZoom, bool selectMode, bool hasDisplay,
                                    QAction *&zoomIn, QAction *&zoomOut, QAction *&zoomFit,
                                    QAction *&zoomActual, QAction *&lockZoomAction,
                                    QAction *&selectRegion)
{
    menu.addSeparator();
    zoomIn = menu.addAction("放大 (+)");
    zoomOut = menu.addAction("缩小 (-)");
    zoomFit = menu.addAction("适应窗口 (0)");
    zoomActual = menu.addAction("实际大小 (1)");
    lockZoomAction = menu.addAction("锁定缩放比 (Ctrl+L)");
    lockZoomAction->setCheckable(true);
    lockZoomAction->setChecked(lockZoom);
    lockZoomAction->setEnabled(hasDisplay);
    for (QAction *a : {zoomIn, zoomOut, zoomFit, zoomActual})
        a->setEnabled(hasDisplay);
    QMenu *mZoomPresets = menu.addMenu("缩放预设");
    mZoomPresets->menuAction()->setEnabled(hasDisplay);
    for (const auto &p : {std::make_pair("50%", 0.5), std::make_pair("100% (实际大小)", 1.0),
                          std::make_pair("200%", 2.0), std::make_pair("400%", 4.0),
                          std::make_pair("800% (像素网格)", 8.0)})
        mZoomPresets->addAction(p.first)->setData(p.second);
    menu.addSeparator();
    selectRegion = menu.addAction("框选区域 (R)");
    selectRegion->setCheckable(true);
    selectRegion->setChecked(selectMode);
    selectRegion->setEnabled(hasDisplay);
}

void addCopyContextActions(QMenu &menu, QAction *&copy, QAction *&copyPath, QAction *&reveal,
                           QMenu *&colorMenu, QAction *&copyHex, QAction *&copyRgb,
                           QAction *&copyFloat, QAction *&copyHsv, QAction *&copyCoord)
{
    copy = menu.addAction("复制图片 (Ctrl+C)");
    copyPath = menu.addAction("复制路径 (Ctrl+Shift+C)");
    reveal = menu.addAction("在资源管理器中显示 (Ctrl+E)");
    colorMenu = menu.addMenu("复制像素值");
    copyHex = colorMenu->addAction("十六进制 (#RRGGBB) (Shift+C)");
    copyRgb = colorMenu->addAction("RGB 值 RGB(r, g, b) (Shift+B)");
    copyCoord = colorMenu->addAction("坐标 (x, y)");
    copyFloat = colorMenu->addAction("归一化浮点 (0.xxx, 0.yyy, 0.zzz)");
    copyHsv = colorMenu->addAction("HSV 值 HSV(h°, s%, v%)");
}

QString copyPixelValue(const PixelRGBA &px, int format, int x = -1, int y = -1)
{
    if (!px.valid && format != 4)
        return QString();
    QString text;
    switch (format)
    {
    case 4:
        if (x >= 0 && y >= 0)
            text = QString("(%1, %2)").arg(x).arg(y);
        break;
    case 0:
        if (px.a < 255)
        {
            text = QString("#%1%2%3%4")
                       .arg(px.r, 2, 16, QChar('0'))
                       .arg(px.g, 2, 16, QChar('0'))
                       .arg(px.b, 2, 16, QChar('0'))
                       .arg(px.a, 2, 16, QChar('0'))
                       .toUpper();
        }
        else
        {
            text = QString("#%1%2%3")
                       .arg(px.r, 2, 16, QChar('0'))
                       .arg(px.g, 2, 16, QChar('0'))
                       .arg(px.b, 2, 16, QChar('0'))
                       .toUpper();
        }
        break;
    case 1:
        if (px.a < 255)
            text = QString("RGBA(%1, %2, %3, %4)").arg(px.r).arg(px.g).arg(px.b).arg(px.a);
        else
            text = QString("RGB(%1, %2, %3)").arg(px.r).arg(px.g).arg(px.b);
        break;
    case 2:
        if (px.a < 255)
            text = QString("(%1, %2, %3, %4)")
                       .arg(px.r / 255.0, 0, 'f', 4)
                       .arg(px.g / 255.0, 0, 'f', 4)
                       .arg(px.b / 255.0, 0, 'f', 4)
                       .arg(px.a / 255.0, 0, 'f', 4);
        else
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
    return text;
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
    QAction *aReveal = nullptr;
    QMenu *mCopyColor = nullptr;
    QAction *aCopyHex = nullptr;
    QAction *aCopyRgb = nullptr;
    QAction *aCopyFloat = nullptr;
    QAction *aCopyHsv = nullptr;
    QAction *aCopyCoord = nullptr;
    addCopyContextActions(menu, aCopy, aCopyPath, aReveal, mCopyColor, aCopyHex, aCopyRgb,
                          aCopyFloat, aCopyHsv, aCopyCoord);
    menu.addSeparator();
    QAction *aSaveAs = menu.addAction("另存为...");
    QAction *aRotateCW = menu.addAction("顺时针旋转 90° 并覆盖原文件 (Ctrl+R)");
    QAction *aRotateCCW = menu.addAction("逆时针旋转 90° 并覆盖原文件 (Ctrl+Shift+R)");
    QAction *aFlipH = menu.addAction("水平翻转并覆盖原文件 (Ctrl+Shift+H)");
    QAction *aFlipV = menu.addAction("垂直翻转并覆盖原文件 (Ctrl+Shift+V)");
    const bool hasPath = !m_currentPath.isEmpty();
    for (QAction *a : {aRotateCW, aRotateCCW, aFlipH, aFlipV, aCopy, aCopyPath, aReveal})
        a->setEnabled(hasPath);
    const bool hasFrame = m_frame && m_frame->isValid();
    if (mCopyColor)
        mCopyColor->menuAction()->setEnabled(hasFrame);
    aSaveAs->setEnabled(hasFrame);
    QAction *aPlay = nullptr, *aRestart = nullptr, *aPrevFrame = nullptr, *aNextFrame = nullptr;
    if (isMultiFrame())
    {
        menu.addSeparator();
        addFrameContextActions(menu, m_sequence.animated, isPlaying(), m_frameIndex, frameCount(),
                               aPlay, aRestart, aPrevFrame, aNextFrame);
    }
    QAction *aZoomIn = nullptr, *aZoomOut = nullptr, *aZoomFit = nullptr, *aZoomActual = nullptr;
    QAction *aLockZoom = nullptr, *aSelectRegion = nullptr;
    addZoomAndSelectContextActions(menu, m_lockZoom, m_selectMode, hasDisplayImage(), aZoomIn,
                                   aZoomOut, aZoomFit, aZoomActual, aLockZoom, aSelectRegion);
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
    QAction *aFullscreen = menu.addAction("全屏 (F11)");
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
    if (chosen == aLockZoom)
    {
        setLockZoom(!m_lockZoom);
        return;
    }
    if (handleContextTransformAction(chosen, aRotateCW, aRotateCCW, aFlipH, aFlipV) ||
        handleContextCopyAction(chosen, aCopy, aCopyPath, aReveal, aCopyHex, aCopyRgb, aCopyFloat,
                                aCopyHsv, event, aCopyCoord) ||
        handleContextImageAction(chosen, aSaveAs, aZoomIn, aZoomOut, aZoomFit, aZoomActual,
                                 aSelectRegion))
        return;
    handleContextNavigationAction(chosen, aNext, aPrev, aOvNone, aOvZebra, aOvFalse, aOvR, aOvG,
                                  aOvB, aOvY, aFullscreen, aOvV);
}

bool ImageViewer::handleContextCopyAction(QAction *chosen, QAction *copy, QAction *copyPath,
                                          QAction *reveal, QAction *copyHex, QAction *copyRgb,
                                          QAction *copyFloat, QAction *copyHsv,
                                          QContextMenuEvent *event, QAction *copyCoord)
{
    if (chosen == copy)
    {
        copyToClipboard();
        emit statusMessageRequested(tr("正在复制图片到剪贴板..."));
        return true;
    }
    if (chosen == copyPath)
    {
        QApplication::clipboard()->setText(m_currentPath);
        emit statusMessageRequested(tr("已复制图片完整路径到剪贴板"));
        return true;
    }
    if (chosen == reveal)
    {
        revealInExplorer();
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
    else if (chosen == copyCoord)
        format = 4;

    if (format >= 0)
    {
        const QPoint pos = event->pos();
        const int ix = static_cast<int>((pos.x() - m_view.offsetX) / m_view.scale);
        const int iy = static_cast<int>((pos.y() - m_view.offsetY) / m_view.scale);
        PixelRGBA px = sampleAnalysisPixel(ix, iy);
        if (!px.valid)
            px = m_lastHoverPixel;
        const QString copied = copyPixelValue(px, format, ix, iy);
        if (!copied.isEmpty())
            emit statusMessageRequested(tr("已复制像素值: %1").arg(copied));
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
    const bool ctrl = (modifiers == Qt::ControlModifier);
    const bool shift = (modifiers == Qt::ShiftModifier);
    const bool shiftCtrl = (modifiers == (Qt::ControlModifier | Qt::ShiftModifier));
    if (ctrl && key == Qt::Key_R)
        return rotateCW();
    if (shiftCtrl && key == Qt::Key_R)
        return rotateCCW();
    if (shiftCtrl && key == Qt::Key_H)
        return flipHorizontal();
    if (shiftCtrl && key == Qt::Key_V)
        return flipVertical();
    if (shift && (key == Qt::Key_C || key == Qt::Key_B))
    {
        PixelRGBA px = m_lastHoverPixel;
        if (!px.valid)
        {
            const QPoint pos = mapFromGlobal(QCursor::pos());
            const int ix = static_cast<int>((pos.x() - m_view.offsetX) / m_view.scale);
            const int iy = static_cast<int>((pos.y() - m_view.offsetY) / m_view.scale);
            px = sampleAnalysisPixel(ix, iy);
        }
        if (px.valid)
        {
            const int format = key == Qt::Key_B ? 1 : 0;
            const QString copied = copyPixelValue(px, format);
            if (!copied.isEmpty())
                emit statusMessageRequested(tr("已复制像素值: %1").arg(copied));
            return true;
        }
    }
    if (ctrl && key == Qt::Key_C)
    {
        copyToClipboard();
        emit statusMessageRequested(tr("正在复制图片到剪贴板..."));
        return true;
    }
    if (ctrl && key == Qt::Key_E)
    {
        revealInExplorer();
        return true;
    }
    if (shiftCtrl && key == Qt::Key_C)
    {
        if (!m_currentPath.isEmpty())
        {
            QApplication::clipboard()->setText(m_currentPath);
            emit statusMessageRequested(tr("已复制图片完整路径到剪贴板"));
            return true;
        }
    }
    return false;
}

void ImageViewer::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    const auto mods = event->modifiers();
    if (handleFrameKey(key, mods) || handleNavigationKey(key) || handleZoomKey(key, mods) ||
        handleModeKey(key, mods))
        return;
    QWidget::keyPressEvent(event);
}

bool ImageViewer::handleNavigationKey(int key)
{
    if (key == Qt::Key_Left || key == Qt::Key_PageUp || key == Qt::Key_Backspace)
        emit requestPrev();
    else if (key == Qt::Key_Right || key == Qt::Key_PageDown || key == Qt::Key_Space)
        emit requestNext();
    else
        return false;
    return true;
}

bool ImageViewer::handleZoomKey(int key, Qt::KeyboardModifiers modifiers)
{
    const auto mods = modifiers & ~Qt::KeyboardModifiers(Qt::KeypadModifier);
    // Shift+0…6 are channel overlays (handleModeKey), not zoom.
    if (mods == Qt::ShiftModifier && key >= Qt::Key_0 && key <= Qt::Key_6)
        return false;
    // Ctrl+F focuses the address bar. Ctrl+1…6 are gallery view modes.
    // Unmodified 1 is 100%, unmodified 2 is 200%. Ctrl+L still locks zoom.
    if ((mods & Qt::ControlModifier) &&
        (key == Qt::Key_F || (key >= Qt::Key_1 && key <= Qt::Key_6)))
        return false;
    const bool lockZoom = ((mods & Qt::ControlModifier) && key == Qt::Key_L) ||
                          (mods == Qt::NoModifier && key == Qt::Key_L);
    if (lockZoom)
    {
        setLockZoom(!m_lockZoom);
        return true;
    }
    if (key == Qt::Key_Plus || key == Qt::Key_Equal)
        zoomIn();
    else if (key == Qt::Key_Minus || key == Qt::Key_Underscore)
        zoomOut();
    else if (key == Qt::Key_0 || key == Qt::Key_F)
        zoomFit();
    else if (key == Qt::Key_1)
        zoomActual();
    else if (key == Qt::Key_2)
        zoomTo(2.0);
    else
        return false;
    return true;
}

bool ImageViewer::handleModeKey(int key, Qt::KeyboardModifiers modifiers)
{
    if (modifiers == Qt::ShiftModifier && key >= Qt::Key_0 && key <= Qt::Key_6)
    {
        static const mviewer::OverlayMode kChannelKeys[] = {
            mviewer::OverlayMode::None,     mviewer::OverlayMode::ChannelR,
            mviewer::OverlayMode::ChannelG, mviewer::OverlayMode::ChannelB,
            mviewer::OverlayMode::ChannelY, mviewer::OverlayMode::ChannelV};
        setOverlayMode(key == Qt::Key_0 ? mviewer::OverlayMode::None
                                        : kChannelKeys[key - Qt::Key_1]);
        return true;
    }
    if (handleTransformKey(key, modifiers))
        return true;
    if (key == Qt::Key_R && !modifiers)
        setSelectMode(!m_selectMode);
    else if (key == Qt::Key_F11)
        toggleFullscreen();
    else if (key == Qt::Key_Escape)
    {
        if (m_selecting || m_selectMode || m_selStart != m_selEnd)
        {
            m_selecting = false;
            m_selStart = m_selEnd = QPoint();
            if (m_selectMode)
                setSelectMode(false);
            update();
            return true;
        }
        if (property("mviewerFullscreenRequested").toBool())
        {
            setFullscreenRequested(false);
            return true;
        }
        close();
    }
    else
        return false;
    return true;
}

void ImageViewer::revealInExplorer()
{
    if (m_currentPath.isEmpty())
        return;
    const QString p = QDir::toNativeSeparators(m_currentPath);
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            QStringList{QStringLiteral("/select,") + p});
#else
    QProcess::startDetached(QStringLiteral("xdg-open"),
                            QStringList() << QFileInfo(m_currentPath).absolutePath());
#endif
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

void ImageViewer::releaseSourceHandles(const QString &path)
{
    if (path.isEmpty())
        return;
    const bool current = (m_currentPath == path || m_provisionalPath == path);
    if (current)
    {
        ++m_requestGen;
        beginImageGeneration();
        cancelCurrentLoad();
        cancelDisplayRequest();
        cancelRoiStats();
    }
    // Neighbor preloads may still hold `path` even when it is not current.
    cancelPreloads();
    cancelDisplayRasterPreloads();
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
