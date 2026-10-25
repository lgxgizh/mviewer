// Right-pane directory history: header buttons, keyboard, and mouse back/forward.
#include "mainwindow_p.h"

#include <QDir>
#include <QKeyEvent>
#include <QMouseEvent>

bool MainWindow::secondaryBrowseOwns(QObject *watched) const
{
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || !m_splitBrowseEnabled || !m_secondaryBrowsePane)
        return false;
    return widget == m_secondaryBrowsePane || m_secondaryBrowsePane->isAncestorOf(widget);
}

void MainWindow::updateSecondaryNavButtons()
{
    if (m_secondaryDirBackBtn)
        m_secondaryDirBackBtn->setEnabled(m_secondaryBrowseHistory.canBack());
    if (m_secondaryDirForwardBtn)
        m_secondaryDirForwardBtn->setEnabled(m_secondaryBrowseHistory.canForward());
    bool canUp = false;
    if (m_secondaryThumbnailPanel && !m_secondaryThumbnailPanel->currentDir().isEmpty())
    {
        QDir parent(m_secondaryThumbnailPanel->currentDir());
        canUp = parent.cdUp();
    }
    if (m_secondaryDirUpBtn)
        m_secondaryDirUpBtn->setEnabled(canUp);
}

void MainWindow::navigateSecondaryBack()
{
    if (!m_secondaryBrowseHistory.canBack())
        return;
    const QString dir = m_secondaryBrowseHistory.back();
    if (!dir.isEmpty())
        openRightDirectory(dir);
}

void MainWindow::navigateSecondaryForward()
{
    if (!m_secondaryBrowseHistory.canForward())
        return;
    const QString dir = m_secondaryBrowseHistory.forward();
    if (!dir.isEmpty())
        openRightDirectory(dir);
}

void MainWindow::navigateSecondaryUp()
{
    if (!m_secondaryThumbnailPanel)
        return;
    QDir parent(m_secondaryThumbnailPanel->currentDir());
    if (!m_secondaryThumbnailPanel->currentDir().isEmpty() && parent.cdUp())
        openRightDirectory(parent.absolutePath());
}

bool MainWindow::routeDirectoryHistoryMouse(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::MouseButtonPress)
        return false;
    auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() != Qt::BackButton && me->button() != Qt::ForwardButton)
        return false;
    const bool back = me->button() == Qt::BackButton;
    if (secondaryBrowseOwns(watched))
    {
        if (back)
            navigateSecondaryBack();
        else
            navigateSecondaryForward();
    }
    else if (back)
        goDirBack();
    else
        goDirForward();
    return true;
}

bool MainWindow::routeSecondaryBrowseKey(QObject *watched, QEvent *event)
{
    if (!event)
        return false;
    const QEvent::Type type = event->type();
    if (type != QEvent::KeyPress && type != QEvent::ShortcutOverride)
        return false;
    if (!secondaryBrowseOwns(watched))
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
        if (cursor == m_secondaryBrowsePane)
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
        navigateSecondaryBack();
    else
        navigateSecondaryForward();
    return true;
}
