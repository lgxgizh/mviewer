#include "plainrawsettings.h"

#include "core/image/rawplain/PlainRawFile.h"

#include <QSettings>
#include <QString>

namespace mviewer::ui
{
namespace
{

QString defaultsKey()
{
    return QString::fromLatin1(kPlainRawDefaultsSettingsKey);
}

QString grayDefaultsKey()
{
    return QString::fromLatin1(kGrayRawDefaultsSettingsKey);
}

} // namespace

mviewer::core::rawplain::Params loadPlainRawDefaults()
{
    namespace raw = mviewer::core::rawplain;
    QSettings settings;
    const QString key = defaultsKey();
    if (!settings.contains(key))
    {
        return raw::PlainRawParamStore::instance().defaults();
    }
    const auto parsed = raw::parseParams(settings.value(key).toString().toStdString());
    if (!parsed || !raw::isValidDefaults(*parsed))
    {
        const raw::Params empty;
        raw::PlainRawParamStore::instance().setDefaults(empty);
        return empty;
    }
    raw::PlainRawParamStore::instance().setDefaults(*parsed);
    return *parsed;
}

void savePlainRawDefaults(const mviewer::core::rawplain::Params &p)
{
    namespace raw = mviewer::core::rawplain;
    raw::PlainRawParamStore::instance().setDefaults(p);
    QSettings settings;
    settings.setValue(defaultsKey(), QString::fromStdString(raw::toString(p)));
}

mviewer::core::rawplain::Params loadGrayRawDefaults()
{
    namespace raw = mviewer::core::rawplain;
    QSettings settings;
    const QString key = grayDefaultsKey();
    if (!settings.contains(key))
    {
        return raw::PlainRawParamStore::instance().defaults(raw::DumpKind::Gray);
    }
    const auto parsed = raw::parseParams(settings.value(key).toString().toStdString());
    if (!parsed || !raw::isValidDefaults(*parsed))
    {
        const raw::Params fallback = raw::grayDefaultParams();
        raw::PlainRawParamStore::instance().setDefaults(raw::DumpKind::Gray, fallback);
        return fallback;
    }
    raw::PlainRawParamStore::instance().setDefaults(raw::DumpKind::Gray, *parsed);
    return *parsed;
}

void saveGrayRawDefaults(const mviewer::core::rawplain::Params &p)
{
    namespace raw = mviewer::core::rawplain;
    raw::PlainRawParamStore::instance().setDefaults(raw::DumpKind::Gray, p);
    QSettings settings;
    settings.setValue(grayDefaultsKey(), QString::fromStdString(raw::toString(p)));
}

} // namespace mviewer::ui
