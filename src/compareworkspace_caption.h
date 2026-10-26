#pragma once

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHash>
#include <QLabel>
#include <QString>
#include <QStringList>

// Hidden filename store for each compare pane. The visible name is the top
// overlay (wrap, no elision). This label stays hidden unless loading or a
// display error needs a status line; it then floats over the bottom edge of
// the pane, outside any layout, so it never shrinks that pane's viewport and
// every pane keeps the same fit (like the filename overlay at the top).
class ComparePaneCaption final : public QLabel
{
  public:
    static constexpr int kOverlayMargin = 6;

    explicit ComparePaneCaption(QWidget *parent) : QLabel(parent)
    {
        setWordWrap(true);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        if (parent)
            parent->installEventFilter(this);
    }

    // Stretch across the pane bottom, height to the wrapped text.
    void relayout()
    {
        QWidget *host = parentWidget();
        if (!host)
            return;
        const int w = qMax(0, host->width() - 2 * kOverlayMargin);
        const int h = qMin(qMax(minimumHeight(), heightForWidth(w)), host->height());
        setGeometry(kOverlayMargin, qMax(0, host->height() - h - kOverlayMargin), w, h);
        raise();
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
        relayout();
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

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == parentWidget() &&
            (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest ||
             event->type() == QEvent::Show))
            relayout();
        return QLabel::eventFilter(watched, event);
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
                        QString otherCand =
                            otherParts.mid(otherParts.size() - levels).join(QLatin1Char('/'));
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
