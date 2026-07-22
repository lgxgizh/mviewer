#pragma once

#include <QWidget>

class QHBoxLayout;
class QToolButton;

/// Breadcrumb-style path navigation bar (M15 Product Shell P0).
///
/// Displays the current directory path as a chain of clickable segments separated
/// by ">" arrows, similar to Windows Explorer / FastStone. Clicking a segment
/// navigates to that directory immediately. Long paths get an "..." overflow
/// button at the front.
///
/// Usage:
///   connect(bar, &BreadcrumbBar::pathSelected, this, &MainWindow::onBreadcrumbPath);
///   bar->setPath("D:\\photos\\2024\\vacation");
class BreadcrumbBar : public QWidget
{
    Q_OBJECT

  public:
    explicit BreadcrumbBar(QWidget *parent = nullptr);

    /// Set the current path. Parses the path into segments and rebuilds buttons.
    void setPath(const QString &path);
    /// Return the currently displayed path.
    QString currentPath() const { return m_currentPath; }

  signals:
    /// Emitted when the user clicks a breadcrumb segment.
    void pathSelected(const QString &path);

  private slots:
    void onSegmentClicked();

  private:
    void rebuild();

    QHBoxLayout *m_layout = nullptr;
    QString m_currentPath;
    QStringList m_segments;
    bool m_overflow = false;  // true when path is too long and the first segment is "..."
    int m_maxVisible = 6;     // max segments before overflow kicks in

    // Style constants
    static constexpr int kArrowSize = 12;
    static constexpr int kMaxButtonWidth = 200;
};
