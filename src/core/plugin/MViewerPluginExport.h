#pragma once

/**
 * @brief Public macro for all MViewer plugin exports.
 *
 * This is the single source of truth for the MVIEWER_PLUGIN_EXPORT macro
 * used by all in-tree and out-of-tree plugins. Place this header on your
 * include path so third-party developers can use it without duplicating
 * the define across every plugin .cpp file.
 *
 * Windows (MSVC): __declspec(dllexport)
 * GCC/Clang:      __attribute__((visibility("default")))
 */
#if defined(_WIN32) || defined(_WIN64)
#  ifndef MVIEWER_PLUGIN_EXPORT
#    define MVIEWER_PLUGIN_EXPORT __declspec(dllexport)
#  endif
#else
#  ifndef MVIEWER_PLUGIN_EXPORT
#    define MVIEWER_PLUGIN_EXPORT __attribute__((visibility("default")))
#  endif
#endif
