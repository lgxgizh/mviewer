#pragma once

#include <QCoreApplication>
#include <QSet>
#include <QString>
#include <QStringList>

// Pure helpers for Split Browse delete-union confirm text. Kept out of
// thumbnailpanel.h (already at the file-size cap) so tests can call them
// without constructing widgets or showing a modal.

struct SplitDeletePrompt
{
    int leftCount = 0;
    int rightCount = 0;
    int totalCount = 0;
};

inline SplitDeletePrompt splitDeletePrompt(const QStringList &left, const QStringList &right)
{
    SplitDeletePrompt prompt;
    prompt.leftCount = left.size();
    prompt.rightCount = right.size();
    QSet<QString> unique;
    unique.reserve(left.size() + right.size());
    for (const QString &path : left)
        unique.insert(path);
    for (const QString &path : right)
        unique.insert(path);
    prompt.totalCount = unique.size();
    return prompt;
}

inline QStringList splitDeleteUnion(const QStringList &left, const QStringList &right)
{
    QStringList out;
    QSet<QString> seen;
    out.reserve(left.size() + right.size());
    seen.reserve(left.size() + right.size());
    auto appendUnique = [&](const QStringList &paths)
    {
        for (const QString &path : paths)
        {
            if (path.isEmpty() || seen.contains(path))
                continue;
            seen.insert(path);
            out.append(path);
        }
    };
    appendUnique(left);
    appendUnique(right);
    return out;
}

inline QString splitDeleteConfirmMessage(const SplitDeletePrompt &prompt, bool splitActive)
{
    if (splitActive && prompt.leftCount > 0 && prompt.rightCount > 0)
    {
        return QCoreApplication::translate(
                   "ThumbnailPanel", "Move %n file(s) to the Recycle Bin? (left: %1, right: %2)",
                   nullptr, prompt.totalCount)
            .arg(prompt.leftCount)
            .arg(prompt.rightCount);
    }
    if (prompt.totalCount == 1)
    {
        return QCoreApplication::translate(
            "ThumbnailPanel", "Are you sure you want to move this file to the Recycle Bin?");
    }
    return QCoreApplication::translate("ThumbnailPanel",
                                       "Are you sure you want to move %1 files to the Recycle Bin?")
        .arg(prompt.totalCount);
}
