#pragma once
// Multi-select file type filter for the Browse gallery (replaces the former
// single-select "typeFilterCombo"). Each row is a checkable format group from
// browseformats.h; only groups with a decodable extension are listed. Clicking
// a row toggles it and keeps the popup open; the closed combo shows a summary.

#include <QComboBox>
#include <QStringList>

class QStandardItemModel;

class FormatFilterCombo : public QComboBox
{
    Q_OBJECT

  public:
    explicit FormatFilterCombo(QWidget *parent = nullptr);

    // Group ids that are unchecked (hidden). Does not emit.
    void setHiddenGroups(const QStringList &hidden);
    QStringList hiddenGroups() const;
    // Toggle one group as a user click would (emits hiddenGroupsChanged).
    void setGroupVisible(const QString &id, bool visible);
    // Text drawn in the closed combo, e.g. "Types: 8/9".
    QString summaryText() const;
    // Rebuild labels and the decodable-extension list (language switch,
    // decoder registry change).
    void retranslate();

  signals:
    void hiddenGroupsChanged(const QStringList &hidden);

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

  private:
    void rebuildItems();
    void toggleRow(int row);

    QStandardItemModel *m_model = nullptr;
    QStringList m_hidden;
};
