#pragma once

#include "compareworkspace_roi_types.h"
#include "domain/Image.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace mviewer::ui
{

struct RoiStatsRow
{
    QStringList cells;
    QString nameToolTip;
    QString stateToolTip;
};

struct RoiStatsView
{
    std::vector<RoiStatsRow> rows;
    QString deltaText;
    ROIMeasurementState state = ROIMeasurementState::Idle;
    QString stateDetail;
};

RoiStatsView presentRoiStats(const ROIStatsBatchResult &result,
                             const std::vector<mviewer::domain::ImageMetadata> &paneMetadata);

} // namespace mviewer::ui
