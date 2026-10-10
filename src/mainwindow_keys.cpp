// MainWindow keyboard routing while a child or the viewer window has focus.
#include "mainwindow_p.h"

namespace mviewer_keys
{
QWidget *textEntryWidget(QWidget *widget);
bool textEntryOwnsKey(QWidget *widget, const QKeyEvent *key);

// keyPressEvent yields printable keys to a focused editor. Unmodified I and M
// are the exception: they toggle the metadata overlay even when focusAddressBar
// left the address bar focused. textEntryOwnsKey stays true so an event whose
// target is the editor still inserts the letter.
bool yieldPrintableToFocusedEntry(const QKeyEvent *key)
{
    if (!key)
        return false;
    const auto mods = key->modifiers() & ~Qt::KeyboardModifiers(Qt::KeypadModifier);
    if (mods != Qt::NoModifier)
        return true;
    const int code = key->key();
    return code != Qt::Key_I && code != Qt::Key_M;
}
} // namespace mviewer_keys

void MainWindow::focusAddressBar()
{
    if (!m_pathEdit)
        return;
    // ImageViewer is a separate top-level window. setFocus() on a widget in an
    // inactive window only stores a pending focus child, so
    // QApplication::focusWidget() stays on the viewer. Raise and activate
    // first, then focus with ActiveWindowFocusReason so the address bar is the
    // focus widget as soon as this window is active. selectAll() is explicit:
    // ActiveWindowFocusReason does not select on its own.
    raise();
    activateWindow();
    m_pathEdit->setFocus(Qt::ActiveWindowFocusReason);
    m_pathEdit->selectAll();
}

bool MainWindow::forwardGlobalShortcut(QObject *watched, QKeyEvent *ke, Qt::KeyboardModifiers mods)
{
    // Bare digit 2 (200%) stays with the viewer. Modified 0–2 still reach
    // MainWindow for ratings and color labels.
    static const QSet<int> viewerOwns = {Qt::Key_Left,  Qt::Key_Right,  Qt::Key_Plus,
                                         Qt::Key_Equal, Qt::Key_Minus,  Qt::Key_0,
                                         Qt::Key_1,     Qt::Key_2,      Qt::Key_F,
                                         Qt::Key_F11,   Qt::Key_Escape, Qt::Key_Underscore};
    static const QList<int> globalKeys = {
        Qt::Key_Space, Qt::Key_M,      Qt::Key_I,       Qt::Key_G, Qt::Key_D,    Qt::Key_F,
        Qt::Key_Tab,   Qt::Key_C,      Qt::Key_P,       Qt::Key_S, Qt::Key_Plus, Qt::Key_Equal,
        Qt::Key_Minus, Qt::Key_0,      Qt::Key_1,       Qt::Key_2, Qt::Key_F11,  Qt::Key_Home,
        Qt::Key_End,   Qt::Key_PageUp, Qt::Key_PageDown};
    const bool isGlobalKey =
        globalKeys.contains(ke->key()) ||
        ((ke->modifiers() & Qt::ControlModifier) &&
         (ke->key() == Qt::Key_C || (ke->key() >= Qt::Key_1 && ke->key() <= Qt::Key_6)));
    if (!isGlobalKey || watched == this)
        return false;
    // Also forward from the image viewer (it has its own keyPressEvent
    // that handles zoom/navigation, but Home/End/PageUp/PageDown and
    // workflow keys like C/S/Space should still reach MainWindow).
    if (watched == m_imageViewer)
    {
        // Alt+D is only a WindowShortcut QAction. keyPressEvent skips QAction
        // dispatch, so the viewer would otherwise drop it.
        if (mods == Qt::AltModifier && ke->key() == Qt::Key_D)
        {
            focusAddressBar();
            return true;
        }
        // Ctrl+F is the address bar. Ctrl+1..6 are view modes, not zoom.
        const bool ctrlGallery =
            (mods & Qt::ControlModifier) &&
            (ke->key() == Qt::Key_F || (ke->key() >= Qt::Key_1 && ke->key() <= Qt::Key_6));
        if (viewerOwns.contains(ke->key()) && !ctrlGallery)
            return false; // let the viewer handle it
    }
    keyPressEvent(ke);
    return true;
}

bool MainWindow::filterKeyPress(QObject *watched, QKeyEvent *ke)
{
    // Editors keep printable keys and Ctrl+C. Returning false lets the widget
    // that owns the event insert the character or copy its own selection.
    if (mviewer_keys::textEntryOwnsKey(qobject_cast<QWidget *>(watched), ke))
        return false;
    if (watched == m_searchEdit)
    {
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
        {
            if (m_thumbnailPanel)
                m_thumbnailPanel->setFocus();
            return true;
        }
        if (ke->key() == Qt::Key_Escape)
        {
            if (!m_searchEdit->text().isEmpty())
                m_searchEdit->clear();
            if (m_thumbnailPanel)
                m_thumbnailPanel->setFocus();
            return true;
        }
        // Letters, digits, and Space are filter text, not shortcuts.
        return false;
    }
    // Animated sequences own bare Space (play/pause). Still images keep
    // Space = quick compare via the global forward below.
    const auto mods = ke->modifiers() & ~Qt::KeyboardModifiers(Qt::KeypadModifier);
    if (watched == m_imageViewer && ke->key() == Qt::Key_Space && mods == Qt::NoModifier &&
        m_imageViewer->sequenceInfo().animated)
        return false;
    // Shift+C / Shift+B copy the pixel under the cursor (hex / RGB). Plain C
    // is compare and plain B is a browse-mode key; those modifiers must reach
    // ImageViewer instead of being consumed as global shortcuts.
    const auto shiftBare = ke->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier |
                                              Qt::AltModifier | Qt::MetaModifier);
    if (shiftBare == Qt::ShiftModifier && (ke->key() == Qt::Key_C || ke->key() == Qt::Key_B))
    {
        if (watched == m_imageViewer)
            return false;
        if (m_imageViewer)
        {
            QApplication::sendEvent(m_imageViewer, ke);
            return true;
        }
        return false;
    }
    // Esc stops a running slideshow before the viewer's own ladder (clear a
    // real drag selection, else close metadata the user opened, else leave
    // the image view and fullscreen together). A click without a drag is not
    // a selection; that stays in ImageViewer.
    if (ke->key() == Qt::Key_Escape && mods == Qt::NoModifier && m_slideshowTimer &&
        m_slideshowTimer->isActive())
    {
        stopSlideshow();
        return true;
    }
    // While the viewer window has focus (e.g. slideshow fullscreen), 'S'
    // still toggles the slideshow; the viewer itself has no such binding.
    if (watched == m_imageViewer && ke->key() == Qt::Key_S && !ke->modifiers())
    {
        toggleSlideshow();
        return true;
    }
    // Gallery Ctrl+L locks viewer zoom. Plain L stays a normal key, and text
    // editors keep Ctrl+L.
    if (mods == Qt::ControlModifier && ke->key() == Qt::Key_L && watched != m_imageViewer)
    {
        if (!mviewer_keys::textEntryWidget(qobject_cast<QWidget *>(watched)) && m_imageViewer)
        {
            QApplication::sendEvent(m_imageViewer, ke);
            return true;
        }
    }
    // Viewer focus: rename, delete, refresh, ratings, color labels, pick, reject.
    if (watched == m_imageViewer)
    {
        const int key = ke->key();
        const bool ctrlShift = mods == (Qt::ControlModifier | Qt::ShiftModifier);
        const bool altOnly = mods == Qt::AltModifier;
        const bool refresh = mods == Qt::NoModifier && key == Qt::Key_F5;
        const bool fileOp = mods == Qt::NoModifier && (key == Qt::Key_F2 || key == Qt::Key_Delete);
        const bool rateOrFlag = ctrlShift && ((key >= Qt::Key_0 && key <= Qt::Key_5) ||
                                              key == Qt::Key_P || key == Qt::Key_X);
        const bool colorLabel = altOnly && key >= Qt::Key_0 && key <= Qt::Key_6;
        if (fileOp || rateOrFlag || colorLabel || refresh)
        {
            keyPressEvent(ke);
            return true;
        }
    }
    // Forward navigation / workflow shortcuts from child widgets so they work
    // regardless of which panel has focus.
    if (forwardGlobalShortcut(watched, ke, mods))
        return true;
    return false;
}
