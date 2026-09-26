#pragma once

#include <QLabel>
#include <QResizeEvent>
#include <QString>
#include <algorithm>

// Compact eliding caption under each compare pane. Full text stays in the
// tooltip; resize re-elides from the stored full string.
class ComparePaneCaption final : public QLabel
{
  public:
    explicit ComparePaneCaption(QWidget *parent) : QLabel(parent)
    {
    }

    void setFullText(const QString &text)
    {
        m_fullText = text;
        setToolTip(text);
        updateText();
    }

    QString fullText() const
    {
        return m_fullText;
    }

  protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateText();
    }

  private:
    void updateText()
    {
        constexpr int kMaxCaptionPixels = 320;
        const int available = std::min(kMaxCaptionPixels, std::max(0, contentsRect().width() - 8));
        QLabel::setText(fontMetrics().elidedText(m_fullText, Qt::ElideMiddle, available));
    }

    QString m_fullText;
};

inline void setComparePaneCaptionText(QLabel *caption, const QString &fullText)
{
    if (!caption)
        return;
    if (auto *elided = dynamic_cast<ComparePaneCaption *>(caption))
    {
        elided->setFullText(fullText);
        return;
    }
    caption->setToolTip(fullText);
    const QFontMetrics fm(caption->font());
    caption->setText(fm.elidedText(fullText, Qt::ElideMiddle, 320));
}
