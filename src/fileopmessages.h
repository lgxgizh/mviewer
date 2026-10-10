#pragma once

#include <QString>

#include <string>

namespace mviewer::ui
{

// UI-only Chinese for file-operation and undo/redo errors. Core strings stay English.
// Known phrases are replaced wherever they occur; anything else is left unchanged.
QString fileOpErrorZh(const QString &message);
QString fileOpErrorZh(const std::string &error);

} // namespace mviewer::ui
