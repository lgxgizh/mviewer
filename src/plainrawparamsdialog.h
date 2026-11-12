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

// ADR 033 / ADR 034: per-file RAW or Gray Parameters dialog. OK applies a
// session-only override for the file; "Use Defaults" drops the override.
class PlainRawParamsDialog : public QDialog
{
    Q_OBJECT

  public:
    PlainRawParamsDialog(const QString &fileName, int64_t fileSize,
                         const mviewer::core::rawplain::Params &initial, bool hasOverride,
                         QWidget *parent = nullptr,
                         mviewer::core::rawplain::DumpKind kind =
                             mviewer::core::rawplain::DumpKind::Sensor);

    mviewer::core::rawplain::Params params() const;
    bool resetRequested() const;
    bool canAccept() const;
    // When false, a file larger than the frame is refused until the user
    // edits width or height. Default true, so existing callers stay trusted.
    void setSizeTrusted(bool trusted);
    // e.g. "4096x3072, 16-bit LSB container: 25165824 bytes expected, file has
    // 25165824 bytes (match)"; names the mismatch when the size differs.
    QString summaryText() const;
    // Combinations from guessFromFileSize(params(), fileSize, kind) that match
    // the file size; double-clicking one applies it.
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
    bool largerSizeConfirmed() const;

    int64_t m_fileSize = 0;
    mviewer::core::rawplain::DumpKind m_kind = mviewer::core::rawplain::DumpKind::Sensor;
    bool m_reset = false;
    bool m_sizeTrusted = true;
    bool m_sizeEdited = false;
    std::vector<mviewer::core::rawplain::Params> m_guesses;
    PlainRawParamsEditor *m_editor = nullptr;
    QLabel *m_summary = nullptr;
    QListWidget *m_guessList = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

// Resolves the file (resolvePlainRawFile). With no override and a known
// frame, reads samples once so a wider Lsb16 depth is detected. Pre-fills
// the dialog from the effective params. Width or height 0: guessCommonSize
// when that matches the file; otherwise 640x480 is an untrusted placeholder
// (a larger file is not accepted until the user edits the size). Size from
// the name or an override stays trusted. Accept -> reset ? clearOverride :
// setOverride. Returns true when the store changed. Not openable ->
// QMessageBox::warning, false.
bool editPlainRawParamsForPath(QWidget *parent, const QString &path);

// One-line translated label for a packing, e.g. "16-bit container, LSB aligned".
QString plainRawPackingLabel(mviewer::core::rawplain::Packing packing);
