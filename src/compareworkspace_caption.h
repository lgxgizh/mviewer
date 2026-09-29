#pragma once

#include <QLabel>
#include <QString>

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
