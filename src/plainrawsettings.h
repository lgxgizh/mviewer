#pragma once

#include "core/image/rawplain/PlainRawFormat.h"

namespace mviewer::ui
{

inline constexpr const char *kPlainRawDefaultsSettingsKey = "plainRaw/defaults";

// Reads QSettings kPlainRawDefaultsSettingsKey (canonical toString text) into
// PlainRawParamStore defaults. Missing / unparsable / !isValidDefaults ->
// Params{} (store untouched when missing). Returns the installed defaults.
mviewer::core::rawplain::Params loadPlainRawDefaults();

// Installs p as store defaults and persists toString(p).
void savePlainRawDefaults(const mviewer::core::rawplain::Params &p);

} // namespace mviewer::ui
