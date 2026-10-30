#pragma once

// Exclusive icon segments bound to a QComboBox. The combo stays the source of
// truth (shortcuts, persistence, tests). A segment may light for several combo
// values; activating it selects that segment's primary value.

#include <QIcon>
#include <QList>
#include <QWidget>

class QAbstractButton;
class QButtonGroup;
class QComboBox;
class QHBoxLayout;

class ViewModeSegment : public QWidget
{
    Q_OBJECT

  public:
    struct Mode
    {
        int data = 0;
        QIcon icon;
        QString toolTip;
        QList<int> matches; // extra combo values that light this segment
    };

    explicit ViewModeSegment(QWidget *parent = nullptr);

    void setModes(const QList<Mode> &modes);
    void bind(QComboBox *combo);
    void setModeToolTip(int data, const QString &toolTip);
    QAbstractButton *buttonForData(int data) const;
    int checkedData() const;

  private:
    void rebuild();
    void syncFromCombo();
    bool modeMatches(const Mode &mode, int current) const;
    void selectData(int data);

    QComboBox *m_combo = nullptr;
    QButtonGroup *m_group = nullptr;
    QHBoxLayout *m_layout = nullptr;
    QList<Mode> m_modes;
};
