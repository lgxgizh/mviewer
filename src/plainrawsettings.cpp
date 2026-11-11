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

} // namespace mviewer::ui
