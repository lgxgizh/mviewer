#include "ThemeIcons.h"

#include "Theme.h"

#include <QColor>
#include <QHash>
#include <QIconEngine>
#include <QImage>
#include <QImageReader>
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
    "open",
    "back",
    "forward",
    "up",
    "refresh",
    "favorite",
    "compare",
    "analysis",
    "histogram",
    "usm",
    "info",
    "search",
    "browse",
    "browse_workspace",
    "rotate_ccw",
    "rotate_cw",
    "split_browse",
    "sidebar",
    "view_grid",
    "view_list",
    "view_filmstrip",
    "filter",
    "sort",
    "more",
    "zoom_in",
    "zoom_out",
    "fit",
    "close",
    "copy",
    "trash",
};

// Native pixel size of each pre-rendered mask, smallest first. The loader
// picks the smallest mask that still covers the device-pixel request so
// 125% and 150% scales are never an enlarged 1x bitmap.
struct MaskLevel
{
    int pixels;
    const char *suffix;
};

constexpr MaskLevel kLevels[] = {
    {kToolbarIconPx, ""},
    {kToolbarIconPx * 3 / 2, "@1.5x"},
    {kToolbarIconPx * 2, "@2x"},
    {kToolbarIconPx * 3, "@3x"},
};
constexpr int kLevelCount = int(sizeof(kLevels) / sizeof(kLevels[0]));

int levelIndexFor(int devicePixels)
{
    for (int i = 0; i < kLevelCount; ++i)
    {
        if (kLevels[i].pixels >= devicePixels)
            return i;
    }
    return kLevelCount - 1;
}

QImage readLevel(const QString &id, int index)
{
    ensureThemeResources();
    QString path = QStringLiteral(":/icons/");
    path += id;
    path += QLatin1String(kLevels[index].suffix);
    path += QStringLiteral(".png");
    QImageReader reader(path);
    QImage image = reader.read();
    if (image.isNull())
        return {};
    // @2x / @3x file names set a ratio. The buffer size is what we scale.
    image.setDevicePixelRatio(1.0);
    return image;
}

QImage tintImage(QImage image, const QColor &color)
{
    if (image.isNull())
        return {};
    image = image.convertToFormat(QImage::Format_ARGB32);
    const int red = color.red();
    const int green = color.green();
    const int blue = color.blue();
    for (int y = 0; y < image.height(); ++y)
    {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        const int width = image.width();
        for (int x = 0; x < width; ++x)
            line[x] = qRgba(red, green, blue, qAlpha(line[x]));
    }
    return image;
}

QPixmap tintedLevel(const char *id, const QColor &color, int index)
{
    const QString key = QString::fromLatin1(id) + QLatin1Char('|') + color.name(QColor::HexArgb) +
                        QLatin1Char('|') + QString::number(kLevels[index].pixels);
    static QHash<QString, QPixmap> cache;
    const auto found = cache.constFind(key);
    if (found != cache.constEnd())
        return found.value();

    QImage image = tintImage(readLevel(QString::fromLatin1(id), index), color);
    QPixmap pixmap;
    if (!image.isNull())
    {
        pixmap = QPixmap::fromImage(image);
        pixmap.setDevicePixelRatio(1.0);
    }
    cache.insert(key, pixmap);
    return pixmap;
}

QPixmap composeIcon(const char *id, const QColor &color, const QSize &logical, qreal dpr)
{
    if (id == nullptr || id[0] == '\0' || logical.isEmpty() || dpr <= 0.0)
        return {};
    const QSize device = logical * dpr;
    if (device.isEmpty())
        return {};

    const int need = qMax(device.width(), device.height());
    const int start = levelIndexFor(need);
    QPixmap mask;
    for (int i = start; i < kLevelCount && mask.isNull(); ++i)
        mask = tintedLevel(id, color, i);
    for (int i = start - 1; i >= 0 && mask.isNull(); --i)
        mask = tintedLevel(id, color, i);
    if (mask.isNull())
        return {};
    if (mask.size() != device)
        mask = mask.scaled(device, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    mask.setDevicePixelRatio(dpr);
    return mask;
}

QColor tintFor(QIcon::Mode mode, QIcon::State state)
{
    ThemeRole role = ThemeRole::TextPrimary;
    if (mode == QIcon::Disabled)
        role = ThemeRole::TextDisabled;
    else if (state == QIcon::On || mode == QIcon::Selected)
        role = ThemeRole::Accent;
    return QColor(Theme::themeColor(role));
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
        if (scale <= 0.0)
            scale = 1.0;
        return composeIcon(m_id.constData(), tintFor(mode, state), size, scale);
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
    QByteArray m_id;
};

QIcon toolbarIcon(const char *id)
{
    if (id == nullptr || id[0] == '\0')
        return {};
    const QColor color(Theme::themeColor(ThemeRole::TextPrimary));
    if (tintedLevel(id, color, 0).isNull())
        return {};
    return QIcon(new ThemedIconEngine(QByteArray(id)));
}

QPixmap toolbarIconPixmap(const char *id)
{
    return toolbarIcon(id).pixmap(QSize(kToolbarIconPx, kToolbarIconPx));
}

} // namespace mviewer::ui
