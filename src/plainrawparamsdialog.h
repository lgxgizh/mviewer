#pragma once

#include "core/image/rawplain/PlainRawFormat.h"

#include <QDialog>
#include <QString>

#include <cstdint>
#include <vector>

class QDialogButtonBox;
class QLabel;
class QListWidget;
class PlainRawParamsEditor;

// ADR 033: per-file RAW Parameters dialog. OK applies a session-only
// override for the file; "Use Defaults" drops the override.
class PlainRawParamsDialog : public QDialog
{
    Q_OBJECT

  public:
    PlainRawParamsDialog(const QString &fileName, int64_t fileSize,
                         const mviewer::core::rawplain::Params &initial, bool hasOverride,
                         QWidget *parent = nullptr);

    mviewer::core::rawplain::Params params() const;
    bool resetRequested() const;
    bool canAccept() const;
    // e.g. "4096x3072, 16-bit LSB container: 25165824 bytes expected, file has
    // 25165824 bytes (match)"; names the mismatch when the size differs.
    QString summaryText() const;
    // Combinations from guessFromFileSize(params(), fileSize) that match the
    // file size; double-clicking one applies it.
    int guessCount() const;
    void applyGuess(int index);
    PlainRawParamsEditor *editor() const
    {
        return m_editor;
    }

  private slots:
    void updateSummary();
    void useDefaults();

  private:
    int64_t m_fileSize = 0;
    bool m_reset = false;
    std::vector<mviewer::core::rawplain::Params> m_guesses;
    PlainRawParamsEditor *m_editor = nullptr;
    QLabel *m_summary = nullptr;
    QListWidget *m_guessList = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

// Resolves the file (resolvePlainRawFile), pre-fills the dialog with the
// effective params (width/height 0 -> 640x480), runs it. Accept -> reset ?
// clearOverride : setOverride(plainRawParamKey(path), params). Returns true
// when the store changed. Not openable -> QMessageBox::warning, false.
bool editPlainRawParamsForPath(QWidget *parent, const QString &path);

// One-line translated label for a packing, e.g. "16-bit container, LSB aligned".
QString plainRawPackingLabel(mviewer::core::rawplain::Packing packing);
