// Third-pane directory history: toolbar actions, keyboard, and mouse back/forward.
#include "mainwindow_p.h"

#include <QDir>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>

bool MainWindow::tertiaryBrowseOwns(QObject *watched) const
{
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || !m_tripleBrowseEnabled || !m_tertiaryBrowsePane)
        return false;
    return widget == m_tertiaryBrowsePane || m_tertiaryBrowsePane->isAncestorOf(widget);
}

bool MainWindow::tertiaryLineEditOwns(QObject *watched) const
{
    if (!watched || !m_tertiaryBrowsePane)
        return false;
    for (QObject *cursor = watched; cursor && cursor != m_tertiaryBrowsePane;
         cursor = cursor->parent())
    {
        if (qobject_cast<QLineEdit *>(cursor) && tertiaryBrowseOwns(cursor))
            return true;
    }
    return false;
}

bool MainWindow::tertiaryEditorKeepsKey(QObject *watched, const QKeyEvent *ke) const
{
    // Letting Ctrl+F through runs the address-bar command and moves focus away
    // from the focused third-pane editor.
    const auto mods = ke->modifiers() & ~Qt::KeyboardModifiers(Qt::KeypadModifier);
    return mods == Qt::ControlModifier && ke->key() == Qt::Key_F && tertiaryLineEditOwns(watched);
}

bool MainWindow::tertiaryNavActive() const
{
    return m_tripleBrowseEnabled && m_tertiaryThumbnailPanel &&
           m_activeBrowsePane == SelectionModel::PaneSlot::Tertiary;
}

// The third pane has no nav buttons of its own (its header matches the left
// address bar); the shared toolbar actions mirror its history while active.
void MainWindow::updateTertiaryNavButtons()
{
    if (!tertiaryNavActive())
        return;
    if (m_actDirBack)
        m_actDirBack->setEnabled(m_tertiaryBrowseHistory.canBack());
    if (m_actDirForward)
        m_actDirForward->setEnabled(m_tertiaryBrowseHistory.canForward());
    QDir parent(m_tertiaryThumbnailPanel->currentDir());
    if (m_actDirUp)
        m_actDirUp->setEnabled(!m_tertiaryThumbnailPanel->currentDir().isEmpty() && parent.cdUp());
}

void MainWindow::navigateTertiaryBack()
{
    if (!m_tertiaryBrowseHistory.canBack())
        return;
    const QString dir = m_tertiaryBrowseHistory.back();
    if (!dir.isEmpty())
        openThirdDirectory(dir);
}

void MainWindow::navigateTertiaryForward()
{
    if (!m_tertiaryBrowseHistory.canForward())
        return;
    const QString dir = m_tertiaryBrowseHistory.forward();
    if (!dir.isEmpty())
        openThirdDirectory(dir);
}

void MainWindow::navigateTertiaryUp()
{
    if (!m_tertiaryThumbnailPanel)
        return;
    QDir parent(m_tertiaryThumbnailPanel->currentDir());
    if (!m_tertiaryThumbnailPanel->currentDir().isEmpty() && parent.cdUp())
        openThirdDirectory(parent.absolutePath());
}

bool MainWindow::routeTertiaryBrowseKey(QObject *watched, QEvent *event)
{
    if (!event)
        return false;
    const QEvent::Type type = event->type();
    if (type != QEvent::KeyPress && type != QEvent::ShortcutOverride)
        return false;
    if (!tertiaryBrowseOwns(watched))
        return false;

    auto *widget = qobject_cast<QWidget *>(watched);
    bool textEntry = false;
    for (QWidget *cursor = widget; cursor; cursor = cursor->parentWidget())
    {
        if (cursor->inherits("QLineEdit") || cursor->inherits("QTextEdit") ||
            cursor->inherits("QPlainTextEdit") || cursor->inherits("QAbstractSpinBox"))
        {
            textEntry = true;
            break;
        }
        if (cursor == m_tertiaryBrowsePane)
            break;
    }

    auto *keyEvent = static_cast<QKeyEvent *>(event);
    const auto mods = keyEvent->modifiers() & ~Qt::KeyboardModifiers(Qt::KeypadModifier);
    const bool back = (mods == Qt::AltModifier && keyEvent->key() == Qt::Key_Left) ||
                      (mods == Qt::NoModifier && keyEvent->key() == Qt::Key_Backspace);
    const bool forward = mods == Qt::AltModifier && keyEvent->key() == Qt::Key_Right;
    if (!back && !forward)
        return false;
    // Editors keep the key. Accepting the override only stops the left-pane shortcut.
    if (textEntry)
    {
        if (type == QEvent::ShortcutOverride)
        {
            event->accept();
            return true;
        }
        return false;
    }
    if (type == QEvent::ShortcutOverride)
    {
        event->accept();
        return true;
    }
    if (back)
        navigateTertiaryBack();
    else
        navigateTertiaryForward();
    return true;
}
