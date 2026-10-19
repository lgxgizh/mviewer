#pragma once

#include <QString>

#include <string>

namespace mviewer::ui
{

// Localizes file-operation and undo/redo errors for the current UI language.
// Core strings stay English; known phrases are translated wherever they occur
// and anything else is left unchanged.
QString fileOpErrorText(const QString &message);
QString fileOpErrorText(const std::string &error);

} // namespace mviewer::ui
