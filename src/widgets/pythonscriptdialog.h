#pragma once

// Run a user-supplied Python script on the current image (report feature 9).
//
// The dialog only picks the script, shows the protocol and presents the result.
// It never imports Python into MViewer and never runs anything without an
// explicit Run.

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

class PythonScriptDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit PythonScriptDialog(QWidget *parent = nullptr);

    /// Image the next Run operates on.
    void setImagePath(const QString &path);
    QString imagePath() const;

  private slots:
    void chooseScript();
    void runScript();

  private:
    void refreshRunState();
    void appendResult(const QString &text);

    QComboBox *m_scripts = nullptr;
    QLabel *m_imageLabel = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QPushButton *m_run = nullptr;
    QString m_imagePath;
    QStringList m_scriptsList;
};
