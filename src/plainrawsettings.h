#pragma once

#include "core/image/rawplain/PlainRawFormat.h"

namespace mviewer::ui
{

inline constexpr const char *kPlainRawDefaultsSettingsKey = "plainRaw/defaults";
inline constexpr const char *kGrayRawDefaultsSettingsKey = "grayRaw/defaults";

// Reads QSettings kPlainRawDefaultsSettingsKey (canonical toString text) into
// PlainRawParamStore defaults. Missing / unparsable / !isValidDefaults ->
// Params{} (store untouched when missing). Returns the installed defaults.
mviewer::core::rawplain::Params loadPlainRawDefaults();

// Installs p as store defaults and persists toString(p).
void savePlainRawDefaults(const mviewer::core::rawplain::Params &p);

// Reads QSettings kGrayRawDefaultsSettingsKey into the Gray defaults.
// Missing -> current Gray defaults (store untouched). Unparsable or
// !isValidDefaults -> grayDefaultParams() installed in the store.
// Returns the installed defaults.
mviewer::core::rawplain::Params loadGrayRawDefaults();

// Installs p as Gray defaults (normalized by the store) and persists toString(p).
void saveGrayRawDefaults(const mviewer::core::rawplain::Params &p);

} // namespace mviewer::ui
