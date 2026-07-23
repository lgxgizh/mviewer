#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// P0-2: the single source of truth for "what is currently selected".
//
// Before this class the current image was tracked independently by MainWindow
// (m_currentImagePath), the thumbnail grid (its own QItemSelectionModel) and the
// keyboard-navigation path — so the views could drift out of sync (e.g. pressing
// the arrow keys advanced the viewer but left the thumbnail highlight behind).
//
// Every panel now reacts to this model instead of maintaining its own notion of
// the current item. It is intentionally tiny: it holds state and emits change
// signals, nothing more (no new "manager" infrastructure).
class SelectionModel : public QObject
{
    Q_OBJECT
  public:
    explicit SelectionModel(QObject *parent = nullptr);

    QString currentImage() const { return m_current; }
    QStringList selection() const { return m_selection; }
    bool isEmpty() const { return m_current.isEmpty(); }

  public slots:
    // Make `path` the current image and the sole selection.
    void setCurrentImage(const QString &path);
    // Replace the multi-selection; `current` becomes the focused item.
    void setSelection(const QStringList &paths, const QString &current);
    void clear();

  signals:
    void currentImageChanged(const QString &path);
    void selectionChanged(const QStringList &paths);

  private:
    QString m_current;
    QStringList m_selection;
};
