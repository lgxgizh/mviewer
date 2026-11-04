#pragma once

#include "core/image/yuv/YuvFormat.h"

#include "core/image/yuv/YuvExtensionMap.h"

#include <string>
#include <vector>

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

inline constexpr const char *kYuvExtensionMapSettingsKey = "yuv/extensionMap";

// Reads QSettings kYuvExtensionMapSettingsKey (QStringList of "ext=pixfmt", pixfmt empty for
// auto-detect) into the core extension map. A missing key keeps the built-in defaults; an
// empty list means the user removed every mapping (.yuv and .y4m stay readable). Returns the
// installed rules.
std::vector<mviewer::core::yuv::ExtensionRule> loadYuvExtensionMapSettings();

// Installs 'rules' (sanitised by the core) and persists what was installed.
std::vector<mviewer::core::yuv::ExtensionRule>
saveYuvExtensionMap(const std::vector<mviewer::core::yuv::ExtensionRule> &rules);

} // namespace mviewer::ui
