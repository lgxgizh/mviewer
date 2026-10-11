#include "ThemeIcons.h"

#include "Theme.h"

#include <QColor>
#include <QHash>
#include <QIconEngine>
#include <QImage>
#include <QPainter>

#include <utility>

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

// Tints at paint time from the active tokens, so toolbar icons follow a live
// light/dark switch without rebuilding the actions that hold them.
class ThemedIconEngine : public QIconEngine
{
  public:
    explicit ThemedIconEngine(QByteArray id) : m_id(std::move(id))
    {
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state,
                         qreal scale) override
    {
        ThemeRole role = ThemeRole::TextPrimary;
        if (mode == QIcon::Disabled)
            role = ThemeRole::TextDisabled;
        else if (state == QIcon::On || mode == QIcon::Selected)
            role = ThemeRole::Accent;
        const QSize device = size * scale;
        const qreal maskScale = qMax(device.width(), device.height()) > kMaskPixels ? 2.0 : 1.0;
        QPixmap mask = tintedIcon(m_id.constData(), QColor(Theme::themeColor(role)), maskScale);
        if (mask.isNull() || device.isEmpty())
            return mask;
        if (mask.size() != device)
            mask = mask.scaled(device, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        mask.setDevicePixelRatio(scale);
        return mask;
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal dpr =
            painter->device() != nullptr ? painter->device()->devicePixelRatioF() : 1.0;
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, dpr));
    }

    QSize actualSize(const QSize &size, QIcon::Mode, QIcon::State) override
    {
        return size;
    }

    QIconEngine *clone() const override
    {
        return new ThemedIconEngine(m_id);
    }

    QString key() const override
    {
        return QStringLiteral("mviewer-themed");
    }

  private:
    static constexpr int kMaskPixels = 18;
    QByteArray m_id;
};

QIcon toolbarIcon(const char *id)
{
    if (id == nullptr || id[0] == '\0')
        return {};
    if (tintedIcon(id, QColor(Theme::themeColor(ThemeRole::TextPrimary)), 1.0).isNull())
        return {};
    return QIcon(new ThemedIconEngine(QByteArray(id)));
}

QPixmap toolbarIconPixmap(const char *id)
{
    return toolbarIcon(id).pixmap(QSize(18, 18));
}

} // namespace mviewer::ui
