#pragma once

// Subjective A/B blind test (report feature 8).
//
// Two images are compared without the observer knowing which is which: the
// order is shuffled from a recorded seed, only one image is visible at a time,
// and the identity is revealed only after both MOS scores are in. The result
// (seed, per-side score, which side was A) is exported so the session is
// reproducible.

#include <QDialog>
#include <QString>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QStackedWidget;

class BlindTestDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit BlindTestDialog(QWidget *parent = nullptr);

    /// Test hook: set both sources without a file dialog.
    void setSources(const QString &a, const QString &b);

    /// True when the reveal is showing (identities no longer hidden).
    bool revealed() const;

  private slots:
    void chooseSourceA();
    void chooseSourceB();
    void toggleSide();
    void finish();

  private:
    void applyRandomization();
    void refreshView();
    void exportResults();

    QComboBox *m_pathA = nullptr;
    QComboBox *m_pathB = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_imageLabel = nullptr;
    QLabel *m_sideLabel = nullptr;
    QSlider *m_mos = nullptr;
    QLabel *m_mosValue = nullptr;
    QPushButton *m_toggle = nullptr;
    QPushButton *m_finish = nullptr;

    QString m_sourceA;
    QString m_sourceB;
    // Side 0/1 as presented to the observer; the mapping to A/B is hidden.
    int m_firstSide = 0;
    int m_secondSide = 1;
    int m_currentSide = 0;
    int m_score[2] = {0, 0};
    unsigned int m_seed = 0;
    bool m_done = false;
};
