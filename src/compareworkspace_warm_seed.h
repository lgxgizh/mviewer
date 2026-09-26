#pragma once

#include <QImage>
#include <QRect>
#include <QSize>

#include <string>
#include <vector>

namespace mviewer::ui
{

struct CompareWarmSeed
{
    std::string path;
    QImage image;
    QSize sourceSize;
    QRect sourceRect;
};

} // namespace mviewer::ui
