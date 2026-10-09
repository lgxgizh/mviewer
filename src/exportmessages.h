#pragma once

#include "core/export/ExportJob.h"

#include <QString>

namespace mviewer::ui
{

// UI-only Chinese for ExportJobResult::message. Core strings stay English.
QString exportResultMessageZh(const mviewer::exportjob::ExportJobResult &result);

} // namespace mviewer::ui
