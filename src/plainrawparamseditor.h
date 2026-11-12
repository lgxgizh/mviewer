#pragma once

#include "core/image/rawplain/PlainRawFormat.h"

#include <QString>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

// ADR 033: editor for plain .raw sensor-dump parameters. Used by
// Preferences > RAW (global defaults; width/height may be 0 = "from file
// name") and by the per-file RAW Parameters dialog (size required).
class PlainRawParamsEditor : public QWidget
{
    Q_OBJECT

  public:
    enum class Mode : std::uint8_t
    {
        Defaults, // width/height spin boxes allow 0 = from the file name
        File      // width/height must be >= 1
    };

    explicit PlainRawParamsEditor(Mode mode, QWidget *parent = nullptr);

    void setParams(const mviewer::core::rawplain::Params &p);
    mviewer::core::rawplain::Params params() const;
    // "" when params() is valid for the mode (isValidDefaults / isValid),
    // else a translated, user-facing reason.
    QString validationError() const;

  signals:
    void paramsChanged();
    // Width or height spin edited by the user. setParams does not emit this.
    void sizeEdited();

  private:
    void buildUi();
    void wireSignals();
    void syncEnabledState();
    void onPackingChanged();

    Mode m_mode;
    QSpinBox *m_width = nullptr;
    QSpinBox *m_height = nullptr;
    QComboBox *m_bitDepth = nullptr;  // 8..16 (item data = bits)
    QComboBox *m_packing = nullptr;   // item data = static_cast<int>(Packing)
    QComboBox *m_byteOrder = nullptr; // item data = static_cast<int>(ByteOrder)
    QSpinBox *m_headerOffset = nullptr;
    QSpinBox *m_rowStride = nullptr; // 0 = tightly packed
    QSpinBox *m_black = nullptr;
    QSpinBox *m_white = nullptr; // 0 = 2^bits - 1
    QComboBox *m_cfa = nullptr;  // item data = static_cast<int>(Cfa)
    QComboBox *m_wb = nullptr;   // item data = static_cast<int>(WhiteBalance)
    QDoubleSpinBox *m_wbR = nullptr;
    QDoubleSpinBox *m_wbB = nullptr;
    QComboBox *m_gainPreset = nullptr; // 1x, 2x, 4x, 8x, Custom
    QDoubleSpinBox *m_gain = nullptr;
    QCheckBox *m_gamma = nullptr;
};
