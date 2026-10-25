#pragma once

// Shared RAW source label for the viewer corner, the status bar, and the
// metadata panel. Qt UI header: core/ stays Qt-free.

#include "domain/Image.h"

#include <QCoreApplication>
#include <QString>

inline QString rawSourceBadgeText(mviewer::domain::RawSource source, int width, int height)
{
    using mviewer::domain::RawSource;
    switch (source)
    {
    case RawSource::EmbeddedPreview:
        return QCoreApplication::translate("RawSource", "RAW · Embedded preview %1×%2")
            .arg(width)
            .arg(height);
    case RawSource::HalfDemosaic:
        return QCoreApplication::translate("RawSource", "RAW · Half demosaic %1×%2")
            .arg(width)
            .arg(height);
    case RawSource::FullDemosaic:
        return QCoreApplication::translate("RawSource", "RAW · Full decode");
    case RawSource::NotRaw:
        break;
    }
    return {};
}
