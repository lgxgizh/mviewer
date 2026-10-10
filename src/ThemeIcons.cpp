#include "ThemeIcons.h"

#include "Theme.h"

#include <QColor>
#include <QHash>
#include <QImage>

static void ensureThemeResources()
{
    Q_INIT_RESOURCE(mviewer);
}

namespace mviewer::ui
{

namespace
{

const char *const kToolbarIconIds[] = {
    "open",    "back",     "forward", "up",     "refresh",    "favorite",
    "compare", "analysis", "search",  "browse", "rotate_ccw", "rotate_cw",
};

QPixmap tintMask(const QPixmap &mask, const QColor &color)
{
    if (mask.isNull())
        return {};

    QImage image = mask.toImage().convertToFormat(QImage::Format_ARGB32);
    const int red = color.red();
    const int green = color.green();
    const int blue = color.blue();
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const QRgb pixel = image.pixel(x, y);
            image.setPixel(x, y, qRgba(red, green, blue, qAlpha(pixel)));
        }
    }

    QPixmap tinted = QPixmap::fromImage(image);
    tinted.setDevicePixelRatio(mask.devicePixelRatio());
    return tinted;
}

QPixmap loadMask(const char *id, qreal dpr)
{
    ensureThemeResources();
    QString name = QString::fromLatin1(id);
    if (dpr > 1.5)
    {
        QPixmap hi(QStringLiteral(":/icons/%1@2x.png").arg(name));
        if (!hi.isNull())
        {
            hi.setDevicePixelRatio(2.0);
            return hi;
        }
    }

    QPixmap lo(QStringLiteral(":/icons/%1.png").arg(name));
    if (!lo.isNull())
        lo.setDevicePixelRatio(1.0);
    return lo;
}

QPixmap tintedIcon(const char *id, const QColor &color, qreal dpr)
{
    QString key = QString::fromLatin1(id) + QLatin1Char('|') + color.name(QColor::HexArgb) +
                  QLatin1Char('|') + QString::number(qRound(dpr * 100.0));
    static QHash<QString, QPixmap> cache;
    const auto found = cache.constFind(key);
    if (found != cache.constEnd())
        return found.value();

    QPixmap tinted = tintMask(loadMask(id, dpr), color);
    cache.insert(key, tinted);
    return tinted;
}

} // namespace

QStringList toolbarIconIds()
{
    QStringList ids;
    for (const char *id : kToolbarIconIds)
        ids.append(QString::fromLatin1(id));
    return ids;
}

QIcon toolbarIcon(const char *id)
{
    if (id == nullptr || id[0] == '\0')
        return {};

    QColor primary(Theme::themeColor(ThemeRole::TextPrimary));
    QColor accent(Theme::themeColor(ThemeRole::Accent));
    QColor disabled(Theme::themeColor(ThemeRole::TextDisabled));
    QPixmap probe = tintedIcon(id, primary, 1.0);
    if (probe.isNull())
        return {};

    QIcon icon;
    const qreal scales[] = {1.0, 2.0};
    for (const qreal dpr : scales)
    {
        QPixmap normal = tintedIcon(id, primary, dpr);
        QPixmap checked = tintedIcon(id, accent, dpr);
        QPixmap dim = tintedIcon(id, disabled, dpr);
        icon.addPixmap(normal, QIcon::Normal, QIcon::Off);
        icon.addPixmap(normal, QIcon::Active, QIcon::Off);
        icon.addPixmap(checked, QIcon::Normal, QIcon::On);
        icon.addPixmap(checked, QIcon::Active, QIcon::On);
        icon.addPixmap(checked, QIcon::Selected, QIcon::Off);
        icon.addPixmap(checked, QIcon::Selected, QIcon::On);
        icon.addPixmap(dim, QIcon::Disabled, QIcon::Off);
        icon.addPixmap(dim, QIcon::Disabled, QIcon::On);
    }
    return icon;
}

QPixmap toolbarIconPixmap(const char *id)
{
    return toolbarIcon(id).pixmap(QSize(18, 18));
}

} // namespace mviewer::ui
