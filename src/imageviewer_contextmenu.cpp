#include "imageviewer.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/analysis/PixelInspector.h"
#include "core/analyzer/Analyzer.h"
#include "core/image/ImageFileRotate.h"
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
#include <QProcess>
#include <QRect>
#include <QResizeEvent>
#include <QSettings>
#include <QTimer>
#include <QTransform>
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

void setContextImageActionAvailability(QAction *copy, QAction *copyPath, QAction *reveal,
                                       QMenu *copyColorMenu, QAction *saveAs, QAction *zoomIn,
                                       QAction *zoomOut, QAction *zoomFit, QAction *zoomActual,
                                       QMenu *zoomPresetsMenu, QAction *selectRegion, bool hasPath,
                                       bool hasFrame, bool hasDisplay)
{
    copy->setEnabled(hasPath);
    copyPath->setEnabled(hasPath);
    if (reveal)
        reveal->setEnabled(hasPath);
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

void addCopyContextActions(QMenu &menu, QAction *&copy, QAction *&copyPath, QAction *&reveal,
                           QMenu *&colorMenu, QAction *&copyHex, QAction *&copyRgb,
                           QAction *&copyFloat, QAction *&copyHsv)
{
    copy = menu.addAction("复制图片 (Ctrl+C)");
    copyPath = menu.addAction("复制路径 (Ctrl+Shift+C)");
    reveal = menu.addAction("在资源管理器中显示 (Ctrl+E)");
    colorMenu = menu.addMenu("复制像素值");
    copyHex = colorMenu->addAction("十六进制 (#RRGGBB) (Shift+C)");
    copyRgb = colorMenu->addAction("RGB 值 RGB(r, g, b)");
    copyFloat = colorMenu->addAction("归一化浮点 (0.xxx, 0.yyy, 0.zzz)");
    copyHsv = colorMenu->addAction("HSV 值 HSV(h°, s%, v%)");
}

QString copyPixelValue(const PixelRGBA &px, int format)
{
    if (!px.valid)
        return QString();
    QString text;
    switch (format)
    {
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
    addCopyContextActions(menu, aCopy, aCopyPath, aReveal, mCopyColor, aCopyHex, aCopyRgb,
                          aCopyFloat, aCopyHsv);
    menu.addSeparator();
    QAction *aSaveAs = menu.addAction("另存为...");
    QAction *aRotateCW = menu.addAction("顺时针旋转 90° 并覆盖原文件 (Ctrl+R)");
    QAction *aRotateCCW = menu.addAction("逆时针旋转 90° 并覆盖原文件 (Ctrl+Shift+R)");
    QAction *aFlipH = menu.addAction("水平翻转并覆盖原文件 (Ctrl+Shift+H)");
    QAction *aFlipV = menu.addAction("垂直翻转并覆盖原文件 (Ctrl+Shift+V)");
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
    setContextImageActionAvailability(aCopy, aCopyPath, aReveal, mCopyColor, aSaveAs, aZoomIn,
                                      aZoomOut, aZoomFit, aZoomActual, mZoomPresets, aSelectRegion,
                                      !m_currentPath.isEmpty(), m_frame && m_frame->isValid(),
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
        handleContextCopyAction(chosen, aCopy, aCopyPath, aReveal, aCopyHex, aCopyRgb, aCopyFloat,
                                aCopyHsv, event) ||
        handleContextImageAction(chosen, aSaveAs, aZoomIn, aZoomOut, aZoomFit, aZoomActual,
                                 aSelectRegion))
        return;
    handleContextNavigationAction(chosen, aNext, aPrev, aOvNone, aOvZebra, aOvFalse, aOvR, aOvG,
                                  aOvB, aOvY, aFullscreen, aOvV);
}

bool ImageViewer::handleContextCopyAction(QAction *chosen, QAction *copy, QAction *copyPath,
                                          QAction *reveal, QAction *copyHex, QAction *copyRgb,
                                          QAction *copyFloat, QAction *copyHsv,
                                          QContextMenuEvent *event)
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
        const QString copied = copyPixelValue(px, format);
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
            const QString copied = copyPixelValue(px, 0);
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
    Q_UNUSED(modifiers); // Ctrl+0/Ctrl+1 intentionally share the same zoom action.
    return true;
}

bool ImageViewer::handleModeKey(int key, Qt::KeyboardModifiers modifiers)
{
    if (modifiers == Qt::ShiftModifier && key >= Qt::Key_1 && key <= Qt::Key_6)
    {
        static const mviewer::OverlayMode kChannelKeys[] = {
            mviewer::OverlayMode::None,     mviewer::OverlayMode::ChannelR,
            mviewer::OverlayMode::ChannelG, mviewer::OverlayMode::ChannelB,
            mviewer::OverlayMode::ChannelY, mviewer::OverlayMode::ChannelV};
        setOverlayMode(kChannelKeys[key - Qt::Key_1]);
        return true;
    }
    if (handleTransformKey(key, modifiers))
        return true;
    if (key == Qt::Key_R && !modifiers)
        setSelectMode(!m_selectMode);
    else if ((key == Qt::Key_F && !modifiers) || key == Qt::Key_F11)
        toggleFullscreen();
    else if (key == Qt::Key_Escape)
        close();
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

bool ImageViewer::rotateCW()
{
    return rotateImage(90);
}

bool ImageViewer::rotateCCW()
{
    return rotateImage(-90);
}

QString ImageViewer::rotateFailureUserMessage(const mviewer::core::ImageFileRotateResult &result)
{
    using mviewer::core::ImageRotateError;
    switch (result.errorCode)
    {
    case ImageRotateError::NotWritable:
    case ImageRotateError::AccessDenied:
        return tr("文件或所在文件夹没有写入权限");
    case ImageRotateError::SharingViolation:
        return tr("文件正在被使用（含本程序解码），无法覆盖");
    case ImageRotateError::UnsupportedFormat:
    {
        std::string suffix = result.error;
        const auto pos = suffix.rfind(": ");
        if (pos != std::string::npos)
            suffix = suffix.substr(pos + 2);
        if (suffix.empty() || suffix == "(none)")
            suffix = "?";
        return tr("暂不支持改写 .%1（当前仅 PNG/JPEG/BMP/WebP）")
            .arg(QString::fromStdString(suffix));
    }
    case ImageRotateError::NotFound:
    case ImageRotateError::EmptyPath:
        return tr("找不到文件");
    case ImageRotateError::None:
    case ImageRotateError::InvalidAngle:
    case ImageRotateError::ReadFailed:
    case ImageRotateError::ConvertFailed:
    case ImageRotateError::RotateFailed:
    case ImageRotateError::EncodeFailed:
    case ImageRotateError::ShortWrite:
    case ImageRotateError::WriteFailed:
        if (result.error.empty())
            return tr("未知错误");
        return QString::fromStdString(result.error);
    }
    return tr("未知错误");
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

    const QString path = m_currentPath;
    releaseSourceHandles(path);

    const auto result = mviewer::core::rotateImageFile(path.toUtf8().toStdString(), normAngle);
    if (!result.ok)
    {
        QMessageBox::warning(
            this, tr("旋转失败"),
            tr("无法旋转图片：%1\n%2").arg(path, rotateFailureUserMessage(result)));
        return false;
    }

    mviewer::core::ImageLoadingFacade::instance().invalidateSource(path.toUtf8().toStdString());
    ThumbnailProvider::invalidateSource(path.toUtf8().toStdString());

    emit fileRotated(path);
    emit statusMessageRequested(tr("已旋转并覆盖原文件 (%1°)").arg(normAngle));
    refreshSource(path);
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

    const QString path = m_currentPath;
    releaseSourceHandles(path);

    const auto result = mviewer::core::flipImageFile(path.toUtf8().toStdString(), horizontal);
    if (!result.ok)
    {
        QMessageBox::warning(
            this, tr("翻转失败"),
            tr("无法翻转图片：%1\n%2").arg(path, rotateFailureUserMessage(result)));
        return false;
    }

    mviewer::core::ImageLoadingFacade::instance().invalidateSource(path.toUtf8().toStdString());
    ThumbnailProvider::invalidateSource(path.toUtf8().toStdString());

    emit fileRotated(path);
    emit statusMessageRequested(horizontal ? tr("已水平翻转并覆盖原文件")
                                           : tr("已垂直翻转并覆盖原文件"));
    refreshSource(path);
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
