#pragma once

#include <QAction>
#include <QKeySequence>
#include <QString>

namespace mviewer::ui
{

inline QString appendShortcutHint(const QString &label, const QString &shortcut)
{
    if (label.isEmpty() || shortcut.isEmpty())
        return label;
    const QString hint = QStringLiteral("(%1)").arg(shortcut);
    if (label.contains(hint))
        return label;
    return QStringLiteral("%1 (%2)").arg(label, shortcut);
}

inline QString appendShortcutHint(const QString &label, const QKeySequence &seq)
{
    if (seq.isEmpty())
        return label;
    return appendShortcutHint(label, seq.toString(QKeySequence::NativeText));
}

inline QString actionShortcutHint(const QAction *action)
{
    if (!action)
        return {};
    const auto shortcuts = action->shortcuts();
    if (!shortcuts.isEmpty() && !shortcuts.first().isEmpty())
        return shortcuts.first().toString(QKeySequence::NativeText);
    if (!action->shortcut().isEmpty())
        return action->shortcut().toString(QKeySequence::NativeText);
    return {};
}

inline QString formatActionLabelWithShortcut(const QAction *action, const QString &customLabel = {})
{
    if (!action)
        return customLabel;
    const QString text = customLabel.isEmpty() ? action->text() : customLabel;
    const QString key = actionShortcutHint(action);
    if (key.isEmpty())
        return text;
    return appendShortcutHint(text, key);
}

} // namespace mviewer::ui
