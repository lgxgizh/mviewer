#pragma once

#include "core/image/yuv/YuvFormat.h"

#include <string>

namespace mviewer::ui
{

constexpr int kMaxSavedYuvParams = 256;
inline constexpr const char *kYuvParamsSettingsKey = "yuv/savedParams";

// Reads QSettings kYuvParamsSettingsKey (QStringList, each "key<TAB>format" where format is
// yuv::toString(Format)) into YuvParamStore. Skips lines without a tab, empty keys, or formats
// that fail parseFormatString / isValid. Returns the number loaded.
int loadYuvParamSettings();

// store.set(key, f) and persist: remove any line for key, append the new line, drop oldest lines
// beyond kMaxSavedYuvParams.
void saveYuvParam(const std::string &key, const mviewer::core::yuv::Format &f);

// store.clear(key) and remove its line.
void clearYuvParam(const std::string &key);

} // namespace mviewer::ui
