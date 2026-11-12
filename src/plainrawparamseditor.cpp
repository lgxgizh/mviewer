#include "plainrawparamseditor.h"

#include "plainrawparamsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVariant>
#include <QWidget>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace
{

using mviewer::core::rawplain::ByteOrder;
using mviewer::core::rawplain::Cfa;
using mviewer::core::rawplain::Packing;
using mviewer::core::rawplain::Params;
using mviewer::core::rawplain::WhiteBalance;

constexpr int kSpinMax = 2147483647;
constexpr int kWidgetCount = 16;
const char *kForced = "plainRawForced";

class SignalGuard
{
  public:
    explicit SignalGuard(std::initializer_list<QObject *> objects)
    {
        for (QObject *object : objects)
        {
            if (m_count >= kWidgetCount)
            {
                break;
            }
            m_objects[static_cast<std::size_t>(m_count)] = object;
            ++m_count;
            if (object != nullptr)
            {
                object->blockSignals(true);
            }
        }
    }

    ~SignalGuard()
    {
        for (int index = 0; index < m_count; ++index)
        {
            QObject *object = m_objects[static_cast<std::size_t>(index)];
            if (object != nullptr)
            {
                object->blockSignals(false);
            }
        }
    }

    SignalGuard(const SignalGuard &) = delete;
    SignalGuard &operator=(const SignalGuard &) = delete;

  private:
    std::array<QObject *, kWidgetCount> m_objects{};
    int m_count = 0;
};

void selectData(QComboBox *box, int data)
{
    const int index = box->findData(data);
    if (index >= 0)
    {
        box->setCurrentIndex(index);
    }
}

// Insert an unlisted depth so OK cannot silently keep the previous item.
void selectBitDepth(QComboBox *box, int depth)
{
    int index = box->findData(depth);
    if (index < 0)
    {
        box->addItem(QCoreApplication::translate("PlainRawParamsEditor", "%1 bit").arg(depth),
                     depth);
        index = box->findData(depth);
    }
    if (index >= 0)
    {
        box->setCurrentIndex(index);
    }
}

int enumData(const QComboBox *box)
{
    return box->currentData().toInt();
}

template <typename Enum> Enum enumFrom(const QComboBox *box)
{
    return static_cast<Enum>(static_cast<std::uint8_t>(enumData(box)));
}

void setSpin(QSpinBox *box, int value)
{
    if (value < box->minimum() || value > box->maximum())
    {
        box->setProperty(kForced, value);
        const int clamped = value < box->minimum() ? box->minimum() : box->maximum();
        box->setValue(clamped);
        return;
    }
    box->setProperty(kForced, QVariant());
    box->setValue(value);
}

int spinValue(const QSpinBox *box)
{
    const QVariant forced = box->property(kForced);
    if (forced.isValid())
    {
        return forced.toInt();
    }
    return box->value();
}

void setSpin64(QSpinBox *box, int64_t value)
{
    if (value < 0 || value > static_cast<int64_t>(kSpinMax))
    {
        box->setProperty(kForced, static_cast<qlonglong>(value));
        const int clamped = value < 0 ? 0 : kSpinMax;
        box->setValue(clamped);
        return;
    }
    box->setProperty(kForced, QVariant());
    box->setValue(static_cast<int>(value));
}

int64_t spinValue64(const QSpinBox *box)
{
    const QVariant forced = box->property(kForced);
    if (forced.isValid())
    {
        return static_cast<int64_t>(forced.toLongLong());
    }
    return static_cast<int64_t>(box->value());
}

void clearForced(QSpinBox *box)
{
    box->setProperty(kForced, QVariant());
}

int presetIndexForGain(double gain)
{
    const double presets[] = {1.0, 2.0, 4.0, 8.0};
    for (int index = 0; index < 4; ++index)
    {
        const double delta = gain - presets[index];
        if (delta < 0.0005 && delta > -0.0005)
        {
            return index;
        }
    }
    return 4;
}

double presetGain(int index)
{
    const double presets[] = {1.0, 2.0, 4.0, 8.0};
    if (index < 0 || index >= 4)
    {
        return 1.0;
    }
    return presets[index];
}

void addBits(QComboBox *box)
{
    for (int depth = 8; depth <= 16; ++depth)
    {
        box->addItem(QCoreApplication::translate("PlainRawParamsEditor", "%1 bit").arg(depth),
                     depth);
    }
}

void addPacking(QComboBox *box)
{
    const Packing packings[] = {Packing::Bits8, Packing::Lsb16, Packing::Msb16, Packing::Mipi10,
                                Packing::Mipi12};
    for (Packing packing : packings)
    {
        box->addItem(plainRawPackingLabel(packing), static_cast<int>(packing));
    }
}

void addByteOrder(QComboBox *box)
{
    box->addItem(QCoreApplication::translate("PlainRawParamsEditor", "Little endian"),
                 static_cast<int>(ByteOrder::Little));
    box->addItem(QCoreApplication::translate("PlainRawParamsEditor", "Big endian"),
                 static_cast<int>(ByteOrder::Big));
}

void addCfa(QComboBox *box)
{
    box->addItem(QCoreApplication::translate("PlainRawParamsEditor", "RGGB"),
                 static_cast<int>(Cfa::RGGB));
    box->addItem(QCoreApplication::translate("PlainRawParamsEditor", "BGGR"),
                 static_cast<int>(Cfa::BGGR));
    box->addItem(QCoreApplication::translate("PlainRawParamsEditor", "GRBG"),
                 static_cast<int>(Cfa::GRBG));
    box->addItem(QCoreApplication::translate("PlainRawParamsEditor", "GBRG"),
                 static_cast<int>(Cfa::GBRG));
    box->addItem(QCoreApplication::translate("PlainRawParamsEditor", "Mono (no colour filter)"),
                 static_cast<int>(Cfa::Mono));
}

void addWhiteBalance(QComboBox *box)
{
    box->addItem(QCoreApplication::translate("PlainRawParamsEditor", "None"),
                 static_cast<int>(WhiteBalance::None));
    box->addItem(QCoreApplication::translate("PlainRawParamsEditor", "Gray world"),
                 static_cast<int>(WhiteBalance::GrayWorld));
    box->addItem(
        QCoreApplication::translate("PlainRawParamsEditor", "White patch (brightest area)"),
        static_cast<int>(WhiteBalance::WhitePatch));
    box->addItem(QCoreApplication::translate("PlainRawParamsEditor", "Manual"),
                 static_cast<int>(WhiteBalance::Manual));
}

void addGainPresets(QComboBox *box)
{
    box->addItem(QCoreApplication::translate("PlainRawParamsEditor", "1x"));
    box->addItem(QCoreApplication::translate("PlainRawParamsEditor", "2x"));
    box->addItem(QCoreApplication::translate("PlainRawParamsEditor", "4x"));
    box->addItem(QCoreApplication::translate("PlainRawParamsEditor", "8x"));
    box->addItem(QCoreApplication::translate("PlainRawParamsEditor", "Custom"));
}

struct SpinRange
{
    int minimum = 0;
    int maximum = 0;
};

QSpinBox *makeSpin(QWidget *parent, SpinRange range, const QString &objectName)
{
    auto *box = new QSpinBox(parent);
    box->setRange(range.minimum, range.maximum);
    box->setObjectName(objectName);
    return box;
}

struct SizeFields
{
    QSpinBox *width = nullptr;
    QSpinBox *height = nullptr;
    QComboBox *bitDepth = nullptr;
    QComboBox *packing = nullptr;
    QComboBox *byteOrder = nullptr;
    QSpinBox *headerOffset = nullptr;
    QSpinBox *rowStride = nullptr;
};

struct LevelFields
{
    QSpinBox *black = nullptr;
    QSpinBox *white = nullptr;
    QComboBox *cfa = nullptr;
    QComboBox *wb = nullptr;
    QDoubleSpinBox *wbR = nullptr;
    QDoubleSpinBox *wbB = nullptr;
    QComboBox *gainPreset = nullptr;
    QDoubleSpinBox *gain = nullptr;
    QCheckBox *gamma = nullptr;
};

SizeFields addSizeRows(QFormLayout *form, QWidget *parent, bool defaults)
{
    SizeFields fields;
    const int sizeMin = defaults ? 0 : 1;
    fields.width = makeSpin(parent, SpinRange{sizeMin, 65536}, QStringLiteral("plainRawWidth"));
    fields.height = makeSpin(parent, SpinRange{sizeMin, 65536}, QStringLiteral("plainRawHeight"));
    if (defaults)
    {
        const QString fromName =
            QCoreApplication::translate("PlainRawParamsEditor", "From file name");
        fields.width->setSpecialValueText(fromName);
        fields.height->setSpecialValueText(fromName);
    }
    form->addRow(QCoreApplication::translate("PlainRawParamsEditor", "Width:"), fields.width);
    form->addRow(QCoreApplication::translate("PlainRawParamsEditor", "Height:"), fields.height);

    fields.bitDepth = new QComboBox(parent);
    fields.bitDepth->setObjectName(QStringLiteral("plainRawBitDepth"));
    addBits(fields.bitDepth);
    form->addRow(QCoreApplication::translate("PlainRawParamsEditor", "Bit depth:"),
                 fields.bitDepth);

    fields.packing = new QComboBox(parent);
    fields.packing->setObjectName(QStringLiteral("plainRawPacking"));
    addPacking(fields.packing);
    form->addRow(QCoreApplication::translate("PlainRawParamsEditor", "Storage:"), fields.packing);

    fields.byteOrder = new QComboBox(parent);
    fields.byteOrder->setObjectName(QStringLiteral("plainRawByteOrder"));
    addByteOrder(fields.byteOrder);
    form->addRow(QCoreApplication::translate("PlainRawParamsEditor", "Byte order:"),
                 fields.byteOrder);

    fields.headerOffset =
        makeSpin(parent, SpinRange{0, kSpinMax}, QStringLiteral("plainRawHeaderOffset"));
    fields.headerOffset->setSuffix(QCoreApplication::translate("PlainRawParamsEditor", " B"));
    form->addRow(QCoreApplication::translate("PlainRawParamsEditor", "Header offset:"),
                 fields.headerOffset);

    fields.rowStride =
        makeSpin(parent, SpinRange{0, kSpinMax}, QStringLiteral("plainRawRowStride"));
    fields.rowStride->setSpecialValueText(
        QCoreApplication::translate("PlainRawParamsEditor", "Tightly packed"));
    form->addRow(QCoreApplication::translate("PlainRawParamsEditor", "Row stride:"),
                 fields.rowStride);
    return fields;
}

LevelFields addLevelRows(QFormLayout *form, QWidget *parent)
{
    LevelFields fields;
    fields.black = makeSpin(parent, SpinRange{0, 65535}, QStringLiteral("plainRawBlack"));
    form->addRow(QCoreApplication::translate("PlainRawParamsEditor", "Black level:"), fields.black);

    fields.white = makeSpin(parent, SpinRange{0, 65535}, QStringLiteral("plainRawWhite"));
    fields.white->setSpecialValueText(
        QCoreApplication::translate("PlainRawParamsEditor", "Auto (2^bits - 1)"));
    form->addRow(QCoreApplication::translate("PlainRawParamsEditor", "White level:"), fields.white);

    fields.cfa = new QComboBox(parent);
    fields.cfa->setObjectName(QStringLiteral("plainRawCfa"));
    addCfa(fields.cfa);
    form->addRow(QCoreApplication::translate("PlainRawParamsEditor", "Bayer pattern:"), fields.cfa);

    fields.wb = new QComboBox(parent);
    fields.wb->setObjectName(QStringLiteral("plainRawWb"));
    addWhiteBalance(fields.wb);
    form->addRow(QCoreApplication::translate("PlainRawParamsEditor", "White balance:"), fields.wb);

    fields.wbR = new QDoubleSpinBox(parent);
    fields.wbR->setObjectName(QStringLiteral("plainRawWbR"));
    fields.wbR->setRange(0.10, 16.00);
    fields.wbR->setSingleStep(0.05);
    fields.wbR->setDecimals(2);
    form->addRow(QCoreApplication::translate("PlainRawParamsEditor", "Red gain:"), fields.wbR);

    fields.wbB = new QDoubleSpinBox(parent);
    fields.wbB->setObjectName(QStringLiteral("plainRawWbB"));
    fields.wbB->setRange(0.10, 16.00);
    fields.wbB->setSingleStep(0.05);
    fields.wbB->setDecimals(2);
    form->addRow(QCoreApplication::translate("PlainRawParamsEditor", "Blue gain:"), fields.wbB);

    fields.gainPreset = new QComboBox(parent);
    fields.gainPreset->setObjectName(QStringLiteral("plainRawGainPreset"));
    addGainPresets(fields.gainPreset);
    fields.gain = new QDoubleSpinBox(parent);
    fields.gain->setObjectName(QStringLiteral("plainRawGain"));
    fields.gain->setRange(0.125, 64.0);
    fields.gain->setDecimals(3);
    fields.gain->setSuffix(QCoreApplication::translate("PlainRawParamsEditor", "x"));
    auto *gainRow = new QWidget(parent);
    auto *gainLayout = new QHBoxLayout(gainRow);
    gainLayout->setContentsMargins(0, 0, 0, 0);
    gainLayout->addWidget(fields.gainPreset);
    gainLayout->addWidget(fields.gain);
    form->addRow(QCoreApplication::translate("PlainRawParamsEditor", "Brightness gain:"), gainRow);

    fields.gamma =
        new QCheckBox(QCoreApplication::translate("PlainRawParamsEditor", "sRGB gamma"), parent);
    fields.gamma->setObjectName(QStringLiteral("plainRawGamma"));
    form->addRow(fields.gamma);
    return fields;
}

} // namespace

PlainRawParamsEditor::PlainRawParamsEditor(Mode mode, QWidget *parent)
    : QWidget(parent), m_mode(mode)
{
    buildUi();
    wireSignals();
    setParams(Params{});
}

void PlainRawParamsEditor::buildUi()
{
    auto *form = new QFormLayout(this);
    const SizeFields sizes = addSizeRows(form, this, m_mode == Mode::Defaults);
    m_width = sizes.width;
    m_height = sizes.height;
    m_bitDepth = sizes.bitDepth;
    m_packing = sizes.packing;
    m_byteOrder = sizes.byteOrder;
    m_headerOffset = sizes.headerOffset;
    m_rowStride = sizes.rowStride;
    const LevelFields levels = addLevelRows(form, this);
    m_black = levels.black;
    m_white = levels.white;
    m_cfa = levels.cfa;
    m_wb = levels.wb;
    m_wbR = levels.wbR;
    m_wbB = levels.wbB;
    m_gainPreset = levels.gainPreset;
    m_gain = levels.gain;
    m_gamma = levels.gamma;
}

void PlainRawParamsEditor::wireSignals()
{
    const auto notify = [this]() { emit paramsChanged(); };
    const auto notifySpin = [this, notify](QSpinBox *box, bool userSize)
    {
        connect(box, &QSpinBox::valueChanged, this,
                [this, box, notify, userSize](int)
                {
                    clearForced(box);
                    if (userSize)
                    {
                        emit sizeEdited();
                    }
                    notify();
                });
    };
    notifySpin(m_width, true);
    notifySpin(m_height, true);
    notifySpin(m_headerOffset, false);
    notifySpin(m_rowStride, false);
    notifySpin(m_black, false);
    notifySpin(m_white, false);

    connect(m_bitDepth, &QComboBox::currentIndexChanged, this, [notify](int) { notify(); });
    connect(m_packing, &QComboBox::currentIndexChanged, this,
            [this, notify](int)
            {
                onPackingChanged();
                notify();
            });
    connect(m_byteOrder, &QComboBox::currentIndexChanged, this, [notify](int) { notify(); });
    connect(m_cfa, &QComboBox::currentIndexChanged, this, [notify](int) { notify(); });
    connect(m_wb, &QComboBox::currentIndexChanged, this,
            [this, notify](int)
            {
                syncEnabledState();
                notify();
            });
    connect(m_wbR, &QDoubleSpinBox::valueChanged, this, [notify](double) { notify(); });
    connect(m_wbB, &QDoubleSpinBox::valueChanged, this, [notify](double) { notify(); });
    connect(m_gainPreset, &QComboBox::currentIndexChanged, this,
            [this, notify](int index)
            {
                if (index >= 0 && index < 4)
                {
                    QSignalBlocker blocker(m_gain);
                    m_gain->setValue(presetGain(index));
                }
                notify();
            });
    connect(m_gain, &QDoubleSpinBox::valueChanged, this,
            [this, notify](double value)
            {
                const int index = presetIndexForGain(value);
                if (m_gainPreset->currentIndex() != index)
                {
                    QSignalBlocker blocker(m_gainPreset);
                    m_gainPreset->setCurrentIndex(index);
                }
                notify();
            });
    connect(m_gamma, &QCheckBox::toggled, this, [notify](bool) { notify(); });
}

void PlainRawParamsEditor::syncEnabledState()
{
    const bool manual = enumFrom<WhiteBalance>(m_wb) == WhiteBalance::Manual;
    m_wbR->setEnabled(manual);
    m_wbB->setEnabled(manual);
}

void PlainRawParamsEditor::onPackingChanged()
{
    const Packing packing = enumFrom<Packing>(m_packing);
    const bool container = packing == Packing::Lsb16 || packing == Packing::Msb16;
    m_byteOrder->setEnabled(container);
    int forcedBits = 0;
    if (packing == Packing::Bits8)
    {
        forcedBits = 8;
    }
    else if (packing == Packing::Mipi10)
    {
        forcedBits = 10;
    }
    else if (packing == Packing::Mipi12)
    {
        forcedBits = 12;
    }
    m_bitDepth->setEnabled(forcedBits == 0);
    if (forcedBits != 0)
    {
        selectData(m_bitDepth, forcedBits);
    }
}

void PlainRawParamsEditor::setParams(const Params &p)
{
    SignalGuard guard({m_width, m_height, m_bitDepth, m_packing, m_byteOrder, m_headerOffset,
                       m_rowStride, m_black, m_white, m_cfa, m_wb, m_wbR, m_wbB, m_gainPreset,
                       m_gain, m_gamma});
    setSpin(m_width, p.width);
    setSpin(m_height, p.height);
    selectBitDepth(m_bitDepth, p.bitDepth);
    selectData(m_packing, static_cast<int>(p.packing));
    selectData(m_byteOrder, static_cast<int>(p.byteOrder));
    setSpin64(m_headerOffset, p.headerOffset);
    setSpin64(m_rowStride, p.rowStride);
    setSpin(m_black, p.blackLevel);
    setSpin(m_white, p.whiteLevel);
    selectData(m_cfa, static_cast<int>(p.cfa));
    selectData(m_wb, static_cast<int>(p.wb));
    m_wbR->setValue(p.wbGainR);
    m_wbB->setValue(p.wbGainB);
    m_gain->setValue(p.gain);
    m_gainPreset->setCurrentIndex(presetIndexForGain(p.gain));
    m_gamma->setChecked(p.srgbGamma);
    onPackingChanged();
    syncEnabledState();
}

Params PlainRawParamsEditor::params() const
{
    Params out;
    out.width = spinValue(m_width);
    out.height = spinValue(m_height);
    out.bitDepth = enumData(m_bitDepth);
    out.packing = enumFrom<Packing>(m_packing);
    out.byteOrder = enumFrom<ByteOrder>(m_byteOrder);
    out.headerOffset = spinValue64(m_headerOffset);
    out.rowStride = spinValue64(m_rowStride);
    out.blackLevel = spinValue(m_black);
    out.whiteLevel = spinValue(m_white);
    out.cfa = enumFrom<Cfa>(m_cfa);
    out.wb = enumFrom<WhiteBalance>(m_wb);
    out.wbGainR = m_wbR->value();
    out.wbGainB = m_wbB->value();
    out.gain = m_gain->value();
    out.srgbGamma = m_gamma->isChecked();
    return out;
}

QString PlainRawParamsEditor::validationError() const
{
    const Params current = params();
    std::string why;
    const bool valid = m_mode == Mode::Defaults
                           ? mviewer::core::rawplain::isValidDefaults(current, &why)
                           : mviewer::core::rawplain::isValid(current, &why);
    if (valid)
    {
        return {};
    }
    return tr("These RAW parameters are not valid: %1").arg(QString::fromStdString(why));
}
