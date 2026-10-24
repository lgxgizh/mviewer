#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// P0-2: the single source of truth for "what is currently selected".
//
// Before this class the current image was tracked independently by MainWindow
// (m_currentImagePath), the thumbnail grid (its own QItemSelectionModel) and the
// keyboard-navigation path — so the views could drift out of sync.
//
// Extended for Split Browse View: tracks primary and secondary pane selection
// slots independently while presenting a transparent merged selection to the
// rest of the application.
class SelectionModel : public QObject
{
    Q_OBJECT
  public:
    enum class PaneSlot : int
    {
        Primary = 0,
        Secondary = 1
    };
    Q_ENUM(PaneSlot)

    explicit SelectionModel(QObject *parent = nullptr);

    QString currentImage() const
    {
        return m_current;
    }
    QStringList selection() const
    {
        return m_selection;
    }
    bool isEmpty() const
    {
        return m_current.isEmpty();
    }
    // P0-2: the locked reference image within a comparison (drives diff base,
    // metadata/analysis panels). Empty when no reference is locked.
    QString focused() const
    {
        return m_focused;
    }
    // P0-2: the image currently under the cursor in the thumbnail gallery.
    QString hovered() const
    {
        return m_hovered;
    }
    // P0-2: the full set of images currently loaded into the compare workspace.
    QStringList compared() const
    {
        return m_compared;
    }

    // Split Browse View extensions
    bool isSplitModeEnabled() const
    {
        return m_splitModeEnabled;
    }
    PaneSlot activePane() const
    {
        return m_activePane;
    }
    const QStringList &paneSelection(PaneSlot pane) const;
    QString paneCurrentImage(PaneSlot pane) const;

  public slots:
    // Make `path` the current image and the sole selection.
    void setCurrentImage(const QString &path);
    // Replace the multi-selection; `current` becomes the focused item.
    void setSelection(const QStringList &paths, const QString &current = QString());
    void clear();
    // P0-2: focus / hover / compared-set — see getters above for meaning.
    void setFocused(const QString &path);
    void setHovered(const QString &path);
    void setCompared(const QStringList &paths);

    // Split browse slots
    void setSplitModeEnabled(bool enabled);
    void setActivePane(PaneSlot pane);
    void setPaneSelection(PaneSlot pane, const QStringList &paths,
                          const QString &current = QString());
    void clearPane(PaneSlot pane);

  signals:
    void currentImageChanged(const QString &path);
    void selectionChanged(const QStringList &paths);
    void focusedChanged(const QString &path);
    void hoveredChanged(const QString &path);
    void comparedChanged(const QStringList &paths);

    void paneSelectionChanged(SelectionModel::PaneSlot pane, const QStringList &panePaths);
    void activePaneChanged(SelectionModel::PaneSlot pane);
    void splitModeChanged(bool enabled);

  private:
    void rebuildMergedSelection(bool emitSignals = true);

    bool m_splitModeEnabled = false;
    PaneSlot m_activePane = PaneSlot::Primary;
    QStringList m_paneSelections[2];
    QString m_paneCurrentImage[2];

    QString m_current;
    QStringList m_selection;
    QString m_focused;
    QString m_hovered;
    QStringList m_compared;
};
