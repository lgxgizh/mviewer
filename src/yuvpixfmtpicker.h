#pragma once

#include "core/image/yuv/YuvFormat.h"

#include <QString>
#include <QWidget>

class QComboBox;
class QFormLayout;
class QLineEdit;

// The YUV dialog's pixel-format controls, split into the questions a user can
// answer from how the file was written instead of one long list of names:
// chroma subsampling, layout (planar / UV interleaved / packed), component
// order, bit depth, endianness (only above 8 bit) and sample alignment (MSB as
// in P010, or LSB; only above 8 bit). Combinations that do not exist are
// disabled; the resulting canonical name is shown read-only.
class YuvPixFmtPicker : public QWidget
{
    Q_OBJECT
  public:
    explicit YuvPixFmtPicker(const mviewer::core::yuv::Format &initial, QWidget *parent = nullptr);

    // Writes layout/subsampling/order/packing/depth/alignment/endianness into 'base'.
    mviewer::core::yuv::Format apply(mviewer::core::yuv::Format base) const;
    // Shows 'f's pixel fields; false (nothing changes) when 'f' has no name.
    bool setFormat(const mviewer::core::yuv::Format &f);
    // Canonical pixel-format name of the current selection (empty if none).
    QString pixFmtName() const;

    QComboBox *subsamplingBox() const
    {
        return m_sub;
    }
    QComboBox *layoutBox() const
    {
        return m_layout;
    }
    QComboBox *orderBox() const
    {
        return m_order;
    }
    QComboBox *depthBox() const
    {
        return m_depth;
    }
    QComboBox *endianBox() const
    {
        return m_endian;
    }
    QComboBox *alignmentBox() const
    {
        return m_align;
    }

  signals:
    void changed();

  private:
    void rebuildConstraints();
    void onEdited();
    void setRowShown(QWidget *field, bool shown);

    QFormLayout *m_form = nullptr;
    QComboBox *m_sub = nullptr;
    QComboBox *m_layout = nullptr;
    QComboBox *m_order = nullptr;
    QComboBox *m_depth = nullptr;
    QComboBox *m_endian = nullptr;
    QComboBox *m_align = nullptr;
    QLineEdit *m_name = nullptr;
    bool m_updating = false;
    bool m_packedOrder = false; // m_order currently lists YUYV/UYVY/YVYU
};
