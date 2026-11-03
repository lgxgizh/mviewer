#pragma once

#include "core/image/yuv/YuvFormat.h"

#include <QDialog>
#include <QString>

#include <cstdint>
#include <utility>
#include <vector>

class QComboBox;
class QDialogButtonBox;
class QFormLayout;
class QLabel;
class QSpinBox;

class YuvParamsDialog : public QDialog
{
    Q_OBJECT

  public:
    YuvParamsDialog(const QString &fileName, int64_t fileSize,
                    const mviewer::core::yuv::Format &initial,
                    const std::vector<std::pair<int, int>> &sizeCandidates, bool hasSaved,
                    QWidget *parent = nullptr);

    mviewer::core::yuv::Format format() const;
    bool resetRequested() const;
    bool canAccept() const;
    QString summaryText() const;

    void setFrameSize(int w, int h);
    void setPixFmt(const QString &name);

  private slots:
    void updateSummary();
    void applySizePreset(int index);
    void forgetSaved();

  private:
    void addFields(QFormLayout *form, const std::vector<std::pair<int, int>> &sizeCandidates,
                   bool hasSaved);
    void wireSignals();
    mviewer::core::yuv::Format m_initial;
    int64_t m_fileSize = 0;
    bool m_reset = false;
    QComboBox *m_preset = nullptr;
    QSpinBox *m_width = nullptr;
    QSpinBox *m_height = nullptr;
    QComboBox *m_pix = nullptr;
    QComboBox *m_matrix = nullptr;
    QComboBox *m_range = nullptr;
    QLabel *m_summary = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

// Opens the file with openYuvFile(utf8 path). Not openable -> QMessageBox::warning, return false.
// y4m -> QMessageBox::information that Y4M files carry their parameters in the file header,
// return false. Otherwise prefill: saved params when YuvParamStore has yuvParamKey(path), else
// info.format (width/height 0 -> 352x288). Run the dialog; on accept: reset -> clearYuvParam,
// else saveYuvParam(yuvParamKey(path), dlg.format()). Returns true when the store changed.
bool editYuvParamsForPath(QWidget *parent, const QString &path);
