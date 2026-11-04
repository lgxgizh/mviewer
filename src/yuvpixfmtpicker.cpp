#include "yuvpixfmtpicker.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QStandardItemModel>

#include <string>

namespace yuv = mviewer::core::yuv;

namespace
{

void setItemEnabled(QComboBox *box, int index, bool enabled)
{
    auto *model = qobject_cast<QStandardItemModel *>(box->model());
    if (model == nullptr || index < 0 || index >= model->rowCount())
        return;
    if (QStandardItem *item = model->item(index))
        item->setEnabled(enabled);
}

bool itemEnabled(const QComboBox *box, int index)
{
    const auto *model = qobject_cast<const QStandardItemModel *>(box->model());
    const QStandardItem *item = model ? model->item(index) : nullptr;
    return item != nullptr && item->isEnabled();
}

void selectData(QComboBox *box, int value)
{
    const int index = box->findData(value);
    if (index >= 0)
        box->setCurrentIndex(index);
}

int dataOf(const QComboBox *box)
{
    return box->currentData().toInt();
}

// Moves a combo off a disabled entry onto the first enabled one.
void keepEnabled(QComboBox *box)
{
    if (itemEnabled(box, box->currentIndex()))
        return;
    for (int i = 0; i < box->count(); ++i)
    {
        if (itemEnabled(box, i))
        {
            box->setCurrentIndex(i);
            return;
        }
    }
}

} // namespace

YuvPixFmtPicker::YuvPixFmtPicker(const yuv::Format &initial, QWidget *parent) : QWidget(parent)
{
    m_form = new QFormLayout(this);
    m_form->setContentsMargins(0, 0, 0, 0);

    m_sub = new QComboBox(this);
    m_sub->setObjectName(QStringLiteral("yuvSubsampling"));
    m_sub->addItem(tr("4:2:0"), int(yuv::Subsampling::S420));
    m_sub->addItem(tr("4:2:2"), int(yuv::Subsampling::S422));
    m_sub->addItem(tr("4:4:4"), int(yuv::Subsampling::S444));
    m_sub->addItem(tr("4:0:0 (luma only)"), int(yuv::Subsampling::S400));
    m_form->addRow(tr("Chroma subsampling:"), m_sub);

    m_layout = new QComboBox(this);
    m_layout->setObjectName(QStringLiteral("yuvLayout"));
    m_layout->addItem(tr("Planar (Y, U, V planes)"), int(yuv::Layout::Planar));
    m_layout->addItem(tr("UV interleaved (semi-planar, NV12 style)"), int(yuv::Layout::SemiPlanar));
    m_layout->addItem(tr("Packed (YUYV style)"), int(yuv::Layout::Packed));
    m_layout->setToolTip(tr("Semi-planar files store a Y plane followed by one plane of "
                            "interleaved U/V pairs (NV12, NV21, P010). Planar files store "
                            "separate U and V planes (I420, YV12). Both have the same frame "
                            "size, so the file size cannot tell them apart."));
    m_form->addRow(tr("Layout:"), m_layout);

    m_order = new QComboBox(this);
    m_order->setObjectName(QStringLiteral("yuvOrder"));
    m_form->addRow(tr("Component order:"), m_order);

    m_depth = new QComboBox(this);
    m_depth->setObjectName(QStringLiteral("yuvBitDepth"));
    for (int d : {8, 10, 12, 16})
        m_depth->addItem(tr("%1-bit").arg(d), d);
    if (m_depth->findData(initial.bitDepth) < 0 && initial.bitDepth > 8 && initial.bitDepth <= 16)
        m_depth->addItem(tr("%1-bit").arg(initial.bitDepth), initial.bitDepth);
    m_form->addRow(tr("Bit depth:"), m_depth);

    m_endian = new QComboBox(this);
    m_endian->setObjectName(QStringLiteral("yuvEndian"));
    m_endian->addItem(tr("Little-endian"), int(yuv::Endian::Little));
    m_endian->addItem(tr("Big-endian"), int(yuv::Endian::Big));
    m_form->addRow(tr("Byte order:"), m_endian);

    m_align = new QComboBox(this);
    m_align->setObjectName(QStringLiteral("yuvAlignment"));
    m_align->addItem(tr("MSB-aligned (P010 style)"), 1);
    m_align->addItem(tr("LSB-aligned"), 0);
    m_align->setToolTip(tr("Where the significant bits sit in each 16-bit sample. P010/P016 "
                           "camera output is MSB-aligned; yuv420p10le is LSB-aligned."));
    m_form->addRow(tr("Sample alignment:"), m_align);

    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("yuvPixFmtName"));
    m_name->setReadOnly(true);
    m_form->addRow(tr("Format name:"), m_name);

    if (!setFormat(initial))
    {
        yuv::Format fallback = initial;
        yuv::applyPixFmtName("yuv420p", fallback);
        setFormat(fallback);
    }

    for (QComboBox *box : {m_sub, m_layout, m_order, m_depth, m_endian, m_align})
        connect(box, &QComboBox::currentIndexChanged, this, &YuvPixFmtPicker::onEdited);
}

void YuvPixFmtPicker::setRowShown(QWidget *field, bool shown)
{
    field->setVisible(shown);
    if (QWidget *label = m_form->labelForField(field))
        label->setVisible(shown);
}

bool YuvPixFmtPicker::setFormat(const yuv::Format &f)
{
    yuv::Format probe = f;
    if (probe.width <= 0 || probe.height <= 0)
        probe.width = probe.height = 16;
    if (!yuv::isValid(probe) || yuv::pixFmtName(probe).empty())
        return false;
    {
        m_updating = true;
        const QSignalBlocker b1(m_sub), b2(m_layout), b3(m_order), b4(m_depth), b5(m_endian),
            b6(m_align);
        selectData(m_sub, int(f.subsampling));
        selectData(m_layout, int(f.layout));
        if (m_depth->findData(f.bitDepth) < 0)
            m_depth->addItem(tr("%1-bit").arg(f.bitDepth), f.bitDepth);
        selectData(m_depth, f.bitDepth);
        rebuildConstraints();
        if (f.layout == yuv::Layout::Packed)
            selectData(m_order, int(f.packing));
        else
            selectData(m_order, int(f.order));
        selectData(m_endian, int(f.endian));
        selectData(m_align, f.msbAligned ? 1 : 0);
        rebuildConstraints();
        m_updating = false;
    }
    m_name->setText(pixFmtName());
    return true;
}

void YuvPixFmtPicker::onEdited()
{
    if (m_updating)
        return;
    {
        m_updating = true;
        const QSignalBlocker b1(m_sub), b2(m_layout), b3(m_order), b4(m_depth), b5(m_endian),
            b6(m_align);
        const auto oldLayout = static_cast<yuv::Layout>(dataOf(m_layout));
        const int oldDepth = dataOf(m_depth);
        rebuildConstraints();
        // Above 8 bit, semi-planar files are nearly always P010-style (MSB);
        // planar ones yuv420p10le-style (LSB): follow the layout's default
        // when the layout or depth changes.
        const auto layout = static_cast<yuv::Layout>(dataOf(m_layout));
        if (layout != oldLayout || dataOf(m_depth) != oldDepth || sender() == m_layout ||
            sender() == m_depth)
            selectData(m_align, layout == yuv::Layout::SemiPlanar ? 1 : 0);
        m_updating = false;
    }
    m_name->setText(pixFmtName());
    emit changed();
}

void YuvPixFmtPicker::rebuildConstraints()
{
    const auto sub = static_cast<yuv::Subsampling>(dataOf(m_sub));
    const bool gray = sub == yuv::Subsampling::S400;

    // Packed exists for 8-bit 4:2:2 only; 4:0:0 has no chroma to interleave.
    const bool packedOk = sub == yuv::Subsampling::S422;
    setItemEnabled(m_layout, m_layout->findData(int(yuv::Layout::SemiPlanar)), !gray);
    setItemEnabled(m_layout, m_layout->findData(int(yuv::Layout::Packed)), packedOk);
    keepEnabled(m_layout);
    const auto layout = static_cast<yuv::Layout>(dataOf(m_layout));
    const bool packed = layout == yuv::Layout::Packed;

    for (int i = 0; i < m_depth->count(); ++i)
        setItemEnabled(m_depth, i, !packed || m_depth->itemData(i).toInt() == 8);
    keepEnabled(m_depth);

    if (packed != m_packedOrder || m_order->count() == 0)
    {
        const int previous = m_order->count() ? dataOf(m_order) : -1;
        m_order->clear();
        if (packed)
        {
            m_order->addItem(QStringLiteral("YUYV"), int(yuv::Packing::YUYV));
            m_order->addItem(QStringLiteral("UYVY"), int(yuv::Packing::UYVY));
            m_order->addItem(QStringLiteral("YVYU"), int(yuv::Packing::YVYU));
        }
        else
        {
            m_order->addItem(tr("U before V (YUV: NV12, I420)"), int(yuv::PlaneOrder::UV));
            m_order->addItem(tr("V before U (YVU: NV21, YV12)"), int(yuv::PlaneOrder::VU));
        }
        m_packedOrder = packed;
        if (previous >= 0 && !packed)
            selectData(m_order, previous);
    }
    m_order->setEnabled(!gray);

    const bool high = dataOf(m_depth) > 8;
    setRowShown(m_endian, high);
    setRowShown(m_align, high && !packed);
}

yuv::Format YuvPixFmtPicker::apply(yuv::Format base) const
{
    base.subsampling = static_cast<yuv::Subsampling>(dataOf(m_sub));
    base.layout = static_cast<yuv::Layout>(dataOf(m_layout));
    base.bitDepth = dataOf(m_depth);
    const bool high = base.bitDepth > 8;
    base.endian = high ? static_cast<yuv::Endian>(dataOf(m_endian)) : yuv::Endian::Little;
    if (base.layout == yuv::Layout::Packed)
    {
        base.packing = static_cast<yuv::Packing>(dataOf(m_order));
        base.order = yuv::PlaneOrder::UV;
        base.msbAligned = false;
    }
    else
    {
        base.packing = yuv::Packing::None;
        base.order = base.subsampling == yuv::Subsampling::S400
                         ? yuv::PlaneOrder::UV
                         : static_cast<yuv::PlaneOrder>(dataOf(m_order));
        base.msbAligned = high && dataOf(m_align) == 1;
    }
    return base;
}

QString YuvPixFmtPicker::pixFmtName() const
{
    yuv::Format f = apply(yuv::Format{});
    f.width = f.height = 16;
    return QString::fromStdString(yuv::pixFmtName(f));
}
