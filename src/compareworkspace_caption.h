#pragma once

#include <QLabel>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QString>
#include <QStringList>

// Hidden filename store under each compare pane. The visible name is the top
// overlay (wrap, no elision). This label stays hidden unless a display error
// needs a status line under the image.
class ComparePaneCaption final : public QLabel
{
  public:
    explicit ComparePaneCaption(QWidget *parent) : QLabel(parent)
    {
        setWordWrap(true);
    }

    void setFullText(const QString &text)
    {
        m_fullText = text;
        if (m_statusText.isEmpty())
        {
            setToolTip(text);
            QLabel::setText(text);
        }
    }

    void setStatusText(const QString &text)
    {
        m_statusText = text;
        if (text.isEmpty())
        {
            clearStatus();
            return;
        }
        QLabel::setText(text);
        setToolTip(text);
        setVisible(true);
    }

    void clearStatus()
    {
        m_statusText.clear();
        QLabel::setText(m_fullText);
        setToolTip(m_fullText);
        setVisible(false);
    }

    QString fullText() const
    {
        return m_fullText;
    }

    bool hasStatus() const
    {
        return !m_statusText.isEmpty();
    }

  private:
    QString m_fullText;
    QString m_statusText;
};

inline void setComparePaneCaptionText(QLabel *caption, const QString &fullText)
{
    if (!caption)
        return;
    if (auto *stored = dynamic_cast<ComparePaneCaption *>(caption))
    {
        stored->setFullText(fullText);
        return;
    }
    caption->setToolTip(fullText);
    caption->setText(fullText);
    caption->setVisible(false);
}

inline void setComparePaneCaptionStatus(QLabel *caption, const QString &status)
{
    if (auto *stored = dynamic_cast<ComparePaneCaption *>(caption))
        stored->setStatusText(status);
}

inline void clearComparePaneCaptionStatus(QLabel *caption)
{
    if (auto *stored = dynamic_cast<ComparePaneCaption *>(caption))
        stored->clearStatus();
}

inline QString comparePaneCaptionFullText(const QLabel *caption)
{
    if (!caption)
        return {};
    if (const auto *stored = dynamic_cast<const ComparePaneCaption *>(caption))
    {
        if (!stored->fullText().isEmpty())
            return stored->fullText();
    }
    return caption->toolTip();
}

inline bool comparePaneCaptionHasStatus(const QLabel *caption)
{
    const auto *stored = dynamic_cast<const ComparePaneCaption *>(caption);
    return stored && stored->hasStatus();
}

inline QStringList disambiguateCaptions(const QStringList &paths)
{
    QStringList result;
    result.reserve(paths.size());
    if (paths.isEmpty())
        return result;

    QHash<QString, int> nameCounts;
    for (const QString &p : paths)
    {
        const QString name = QFileInfo(p).fileName();
        nameCounts[name]++;
    }

    for (const QString &p : paths)
    {
        const QString fileName = QFileInfo(p).fileName();
        if (nameCounts.value(fileName, 0) <= 1)
        {
            result.append(fileName);
            continue;
        }

        QString normalized = QDir::fromNativeSeparators(p);
        QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.size() <= 1)
        {
            result.append(fileName);
            continue;
        }

        QString best = fileName;
        for (int levels = 2; levels <= parts.size(); ++levels)
        {
            QString candidate = parts.mid(parts.size() - levels).join(QLatin1Char('/'));
            bool unique = true;
            for (const QString &other : paths)
            {
                if (other == p)
                    continue;
                if (QFileInfo(other).fileName() == fileName)
                {
                    QString otherNorm = QDir::fromNativeSeparators(other);
                    QStringList otherParts = otherNorm.split(QLatin1Char('/'), Qt::SkipEmptyParts);
                    if (levels <= otherParts.size())
                    {
                        QString otherCand = otherParts.mid(otherParts.size() - levels).join(QLatin1Char('/'));
                        if (otherCand == candidate)
                        {
                            unique = false;
                            break;
                        }
                    }
                }
            }
            if (unique)
            {
                best = candidate;
                break;
            }
            best = candidate;
        }
        result.append(best);
    }
    return result;
}
