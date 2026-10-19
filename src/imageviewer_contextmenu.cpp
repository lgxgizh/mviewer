#include "imageviewer.h"
#include "metadataoverlay.h"
#include "shortcutdigits.h"

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
#include <QCoreApplication>
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
        play = menu.addAction(playing ? "Pause" : "Play");
        restart = menu.addAction("Restart");
        previous = menu.addAction("Previous Frame (,)");
        next = menu.addAction("Next frame (.)");
        return;
    }

    restart = menu.addAction("First Page");
    previous = menu.addAction("Previous Page (,)");
    next = menu.addAction("Next Page (.)");
    restart->setEnabled(frameIndex > 0);
    previous->setEnabled(frameIndex > 0);
    next->setEnabled(frameIndex + 1 < frameCount);
}

void addOverlayContextActions(QMenu &menu, mviewer::OverlayMode mode, QAction *&none,
                              QAction *&zebra, QAction *&falseColor, QAction *&channelR,
                              QAction *&channelG, QAction *&channelB, QAction *&channelY,
                              QAction *&channelV)
{
    none = menu.addAction(QCoreApplication::translate("ImageViewer", "No Overlay (Shift+1)"));
    zebra =
        menu.addAction(QCoreApplication::translate("ImageViewer", "Over/Under-Exposure &Zebra"));
    falseColor = menu.addAction(QCoreApplication::translate("ImageViewer", "False &Color"));
    channelR = menu.addAction(QCoreApplication::translate("ImageViewer", "R Channel (Shift+2)"));
    channelG = menu.addAction(QCoreApplication::translate("ImageViewer", "G Channel (Shift+3)"));
    channelB = menu.addAction(QCoreApplication::translate("ImageViewer", "B Channel (Shift+4)"));
    channelY = menu.addAction(QCoreApplication::translate("ImageViewer", "Y Luminance (Shift+5)"));
    channelV = menu.addAction(QCoreApplication::translate("ImageViewer", "V Value (Shift+6)"));
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
    zoomIn = menu.addAction(QCoreApplication::translate("ImageViewer", "Zoom In (+)"));
    zoomOut = menu.addAction(QCoreApplication::translate("ImageViewer", "Zoom out (-)"));
    zoomFit = menu.addAction(QCoreApplication::translate("ImageViewer", "Fit to window (0 / F)"));
    zoomActual = menu.addAction(QCoreApplication::translate("ImageViewer", "Actual Size 100% (1)"));
    lockZoomAction =
        menu.addAction(QCoreApplication::translate("ImageViewer", "Lock Zoom Ratio (Ctrl+L)"));
    lockZoomAction->setCheckable(true);
    lockZoomAction->setChecked(lockZoom);
    lockZoomAction->setEnabled(hasDisplay);
    for (QAction *a : {zoomIn, zoomOut, zoomFit, zoomActual})
        a->setEnabled(hasDisplay);
    QMenu *mZoomPresets = menu.addMenu(QCoreApplication::translate("ImageViewer", "Zoom Presets"));
    mZoomPresets->menuAction()->setEnabled(hasDisplay);
    for (const auto &p :
         {std::make_pair("50%", 0.5),
          std::make_pair(QT_TRANSLATE_NOOP("ImageViewer", "100% (Actual Size)"), 1.0),
          std::make_pair("200%", 2.0), std::make_pair("400%", 4.0),
          std::make_pair(QT_TRANSLATE_NOOP("ImageViewer", "800% (Pixel Grid)"), 8.0)})
        mZoomPresets->addAction(QCoreApplication::translate("ImageViewer", p.first))
            ->setData(p.second);
    menu.addSeparator();
    selectRegion =
        menu.addAction(QCoreApplication::translate("ImageViewer", "Marquee selection (R)"));
    selectRegion->setCheckable(true);
    selectRegion->setChecked(selectMode);
    selectRegion->setEnabled(hasDisplay);
}

void addCopyContextActions(QMenu &menu, QAction *&copy, QAction *&copyPath, QAction *&copyName,
                           QAction *&reveal, QMenu *&colorMenu, QAction *&copyHex,
                           QAction *&copyRgb, QAction *&copyFloat, QAction *&copyHsv,
                           QAction *&copyCoord)
{
    copy = menu.addAction(QCoreApplication::translate("ImageViewer", "Copy Image (Ctrl+C)"));
    copyPath =
        menu.addAction(QCoreApplication::translate("ImageViewer", "Copy Path (Ctrl+Shift+C)"));
    copyName = menu.addAction(QCoreApplication::translate("ImageViewer", "Copy filename"));
    reveal =
        menu.addAction(QCoreApplication::translate("ImageViewer", "Show in File Manager (Ctrl+E)"));
    colorMenu = menu.addMenu(QCoreApplication::translate("ImageViewer", "Copy Pixel Value"));
    copyHex =
        colorMenu->addAction(QCoreApplication::translate("ImageViewer", "Hex (#RRGGBB) (Shift+C)"));
    copyRgb = colorMenu->addAction(
        QCoreApplication::translate("ImageViewer", "RGB Value RGB(r, g, b) (Shift+B)"));
    copyCoord =
        colorMenu->addAction(QCoreApplication::translate("ImageViewer", "Coordinates (x, y)"));
    copyFloat = colorMenu->addAction(
        QCoreApplication::translate("ImageViewer", "Normalized Float (0.xxx, 0.yyy, 0.zzz)"));
    copyHsv = colorMenu->addAction(
        QCoreApplication::translate("ImageViewer", "HSV Value HSV(h°, s%, v%)"));
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
    QMenu *analyzeMenu = menu.addMenu(QCoreApplication::translate("ImageViewer", "Analyze"));
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
            analyzeMenu
                ->addAction(QCoreApplication::translate("ImageViewer", "(No analyzers available)"))
                ->setEnabled(false);
    }
    else
    {
        analyzeMenu->addAction(QCoreApplication::translate("ImageViewer", "(Open an image first)"))
            ->setEnabled(false);
    }
}
} // namespace

void ImageViewer::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *aCopy = nullptr;
    QAction *aCopyPath = nullptr;
    QAction *aCopyName = nullptr;
    QAction *aReveal = nullptr;
    QMenu *mCopyColor = nullptr;
    QAction *aCopyHex = nullptr;
    QAction *aCopyRgb = nullptr;
    QAction *aCopyFloat = nullptr;
    QAction *aCopyHsv = nullptr;
    QAction *aCopyCoord = nullptr;
    addCopyContextActions(menu, aCopy, aCopyPath, aCopyName, aReveal, mCopyColor, aCopyHex,
                          aCopyRgb, aCopyFloat, aCopyHsv, aCopyCoord);
    menu.addSeparator();
    QAction *aSaveAs = menu.addAction("Save As...");
    QAction *aUsmCompare = menu.addAction(tr("USM Sharpen Compare(&U)... (Ctrl+U)"));
    QAction *aRotateCW = menu.addAction(tr("Rotate 90° Clockwise and Overwrite Original (Ctrl+R)"));
    QAction *aRotateCCW =
        menu.addAction(tr("Rotate 90° Counterclockwise and Overwrite Original (Ctrl+Shift+R)"));
    QAction *aFlipH = menu.addAction(tr("Flip Horizontal and Overwrite Original (Ctrl+Shift+H)"));
    QAction *aFlipV = menu.addAction(tr("Flip Vertical and Overwrite Original (Ctrl+Shift+V)"));
    const bool hasPath = !m_currentPath.isEmpty();
    for (QAction *a : {aRotateCW, aRotateCCW, aFlipH, aFlipV, aCopy, aCopyPath, aCopyName, aReveal})
        a->setEnabled(hasPath);
    const bool hasFrame = m_frame && m_frame->isValid();
    if (mCopyColor)
        mCopyColor->menuAction()->setEnabled(hasFrame);
    aSaveAs->setEnabled(hasFrame);
    aUsmCompare->setEnabled(hasPath && (hasFrame || hasDisplayImage()));
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

    // A-7.3: "Analyze" submenu — list every registered analyzer for one-click run.
    QList<QAction *> analyzeActions;
    populateAnalyzeSubmenu(menu, m_frame, analyzeActions);
    menu.addSeparator();
    QAction *aNext = menu.addAction("Next (→)");
    QAction *aPrev = menu.addAction("Previous (←)");
    const bool hasBrowsePosition = m_currentIndex >= 0 && m_currentIndex < m_fileList.size();
    aNext->setEnabled(hasBrowsePosition && m_currentIndex + 1 < m_fileList.size());
    aPrev->setEnabled(hasBrowsePosition && m_currentIndex > 0);
    menu.addSeparator();
    const bool isFull = property("mviewerFullscreenRequested").toBool() || isFullScreen();
    QAction *aFullscreen =
        menu.addAction(isFull ? tr("Exit Fullscreen (F11)") : tr("Fullscreen (F11)"));
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
    if (chosen == aUsmCompare)
    {
        emit usmCompareRequested(m_currentPath);
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
                                aCopyHsv, event, aCopyCoord, aCopyName) ||
        handleContextImageAction(chosen, aSaveAs, aZoomIn, aZoomOut, aZoomFit, aZoomActual,
                                 aSelectRegion))
        return;
    handleContextNavigationAction(chosen, aNext, aPrev, aOvNone, aOvZebra, aOvFalse, aOvR, aOvG,
                                  aOvB, aOvY, aFullscreen, aOvV);
}

bool ImageViewer::handleContextCopyAction(QAction *chosen, QAction *copy, QAction *copyPath,
                                          QAction *reveal, QAction *copyHex, QAction *copyRgb,
                                          QAction *copyFloat, QAction *copyHsv,
                                          QContextMenuEvent *event, QAction *copyCoord,
                                          QAction *copyName)
{
    if (chosen == copy)
    {
        copyToClipboard();
        emit statusMessageRequested(tr("Copying image to clipboard..."));
        return true;
    }
    if (chosen == copyPath)
    {
        QApplication::clipboard()->setText(QDir::toNativeSeparators(m_currentPath));
        emit statusMessageRequested(tr("Copied the full image path to the clipboard"));
        return true;
    }
    if (chosen == copyName)
    {
        QApplication::clipboard()->setText(QFileInfo(m_currentPath).fileName());
        emit statusMessageRequested(tr("Copied image filename to clipboard"));
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
            emit statusMessageRequested(tr("Copied pixel value: %1").arg(copied));
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
                emit statusMessageRequested(tr("Copied pixel value: %1").arg(copied));
            return true;
        }
    }
    if (ctrl && key == Qt::Key_C)
    {
        copyToClipboard();
        emit statusMessageRequested(tr("Copying image to clipboard..."));
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
            QApplication::clipboard()->setText(QDir::toNativeSeparators(m_currentPath));
            emit statusMessageRequested(tr("Copied the full image path to the clipboard"));
            return true;
        }
    }
    return false;
}

void ImageViewer::keyPressEvent(QKeyEvent *event)
{
    // Shift+digit arrives as Key_Exclam/Key_At/... on Windows.
    const int key = mviewer::ui::normalizedShortcutKey(event);
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
        setOverlayMode(mviewer::ui::channelOverlayForDigit(key - Qt::Key_0));
        return true;
    }
    if (handleTransformKey(key, modifiers))
        return true;
    if (key == Qt::Key_R && !modifiers)
        setSelectMode(!m_selectMode);
    else if (modifiers == Qt::ControlModifier && key == Qt::Key_U)
    {
        if (!m_currentPath.isEmpty())
        {
            emit usmCompareRequested(m_currentPath);
            return true;
        }
    }
    else if (key == Qt::Key_F11)
        toggleFullscreen();
    else if (key == Qt::Key_Escape)
        return handleViewerEscape();
    else
        return false;
    return true;
}

bool ImageViewer::hasRealSelection() const
{
    // Same threshold as the ROI commit: QRect's inclusive edge makes a click
    // 1×1, and a couple of pixels of jitter is still not a drag.
    const QRect region = QRect(m_selStart, m_selEnd).normalized();
    return region.width() > 5 && region.height() > 5;
}

bool ImageViewer::clearRealSelectionForEscape()
{
    if (!hasRealSelection())
    {
        if (m_selecting || m_selStart != m_selEnd)
        {
            m_selecting = false;
            m_selStart = m_selEnd = QPoint();
            update();
        }
        return false;
    }
    m_selecting = false;
    m_selStart = m_selEnd = QPoint();
    if (m_selectMode)
        setSelectMode(false);
    else
        update();
    return true;
}

bool ImageViewer::dismissOpenMetadata()
{
    bool dismissed = false;
    if (auto *overlay = findChild<MetadataOverlay *>(QString(), Qt::FindDirectChildrenOnly))
    {
        if (overlay->isVisible())
        {
            overlay->hide();
            dismissed = true;
        }
    }
    // The floating panel is a tool window parented to the main window.
    const QWidgetList tops = QApplication::topLevelWidgets();
    for (QWidget *top : tops)
    {
        if (top->isVisible() && top->inherits("MetadataPanel"))
        {
            top->hide();
            dismissed = true;
        }
    }
    return dismissed;
}

bool ImageViewer::handleViewerEscape()
{
    // Slideshow is stopped earlier, while the key is still in MainWindow's
    // filter. A real drag is next, then metadata the user opened. One Esc
    // then returns to Browse. Leaving fullscreen alone stranded the window:
    // showNormal() can move focus so the next Esc never reaches this viewer.
    // setFullscreenRequested(false) restores the windowed frame and drops a
    // stale WindowFullScreen bit before hide(), so the next F11 toggle does
    // not restore the monitor frame.
    if (clearRealSelectionForEscape())
        return true;
    if (dismissOpenMetadata())
        return true;
    if (property("mviewerFullscreenRequested").toBool() || isFullScreen() ||
        windowState().testFlag(Qt::WindowFullScreen))
        setFullscreenRequested(false);
    close();
    return true;
}

void ImageViewer::commitSelectionDrag()
{
    m_selecting = false;
    if (!hasRealSelection())
    {
        m_selStart = m_selEnd = QPoint();
        update();
        return;
    }
    const QRect r = selectedRegion();
    // ROI stats come from the decoded frame, off the UI thread.
    if (!(m_view.scale > 0.0))
        return;
    const QRect imgRect =
        QRect(static_cast<int>(std::floor((r.x() - m_view.offsetX) / m_view.scale)),
              static_cast<int>(std::floor((r.y() - m_view.offsetY) / m_view.scale)),
              static_cast<int>(std::round(r.width() / m_view.scale)),
              static_cast<int>(std::round(r.height() / m_view.scale)))
            .normalized();
    const QSize bounds = displaySize();
    const QRect valid = bounds.isEmpty()
                            ? QRect()
                            : imgRect.intersected(QRect(0, 0, bounds.width(), bounds.height()));
    if (!valid.isEmpty())
    {
        scheduleRoiStats(valid);
        emit selectionChanged(valid);
    }
    else
    {
        cancelRoiStats();
        emit selectionChanged(QRect());
    }
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
                this, isMultiFrame() ? tr("Save Current Frame/Page As") : tr("Save As"),
                defaultName, "PNG (*.png);;JPEG (*.jpg);;BMP (*.bmp);;WebP (*.webp)");
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
    const bool current =
        browsePathEquals(m_currentPath, path) || browsePathEquals(m_provisionalPath, path);
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
