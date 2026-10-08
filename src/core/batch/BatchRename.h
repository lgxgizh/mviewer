#pragma once

#include <string>
#include <vector>

namespace mviewer::core
{

// Batch-rename settings shared by the batch dialog and the export dialog.
// Order of application for one file: find/replace on the stem (base name
// without extension) → rename pattern ({name} is the replaced stem) → ".ext".
struct BatchRenameOptions
{
    std::string pattern;        // e.g. "{name}_batched_{seq:3}"; empty = keep the stem
    std::string find;           // literal text or regular expression; empty = no replace
    std::string replace;        // replacement; regex mode supports \1, \2 … references
    bool useRegex = false;      // false = literal text, true = QRegularExpression
    bool caseSensitive = false; // false = case-insensitive match
};

struct BatchRenameResult
{
    std::string originalPath;
    std::string newName; // final file name including extension
    bool valid = true;
    std::string errorMessage;
};

// Find/replace on `baseName`. An empty `find` returns `baseName` unchanged.
// Literal mode replaces every occurrence; regex mode replaces every match and
// expands capture-group references. An invalid regex returns `baseName`
// unchanged and stores QRegularExpression::errorString() in `errorMessage`.
std::string applyFindReplace(const std::string &baseName, const std::string &find,
                             const std::string &replace, bool useRegex, bool caseSensitive,
                             std::string *errorMessage = nullptr);

// Empty when the options can be applied; otherwise a Chinese message such as
// 「正则表达式无效：…」.
std::string findReplaceError(const BatchRenameOptions &options);

// Empty when `fileName` is a usable Windows file name; otherwise a Chinese
// message describing the first problem (empty, reserved name, bad characters…).
std::string fileNameError(const std::string &fileName);

// Final output file name for one file: find/replace → pattern → ".ext".
// `ext` has no leading dot; an empty `ext` appends nothing.
std::string composeRenamedFileName(const std::string &baseName, const std::string &ext,
                                   const BatchRenameOptions &options, int index, int total);

// Renames one path. `targetExt` (no dot) replaces the source extension when the
// output is re-encoded; empty keeps the source extension.
BatchRenameResult applyBatchRename(const std::string &originalPath,
                                   const BatchRenameOptions &options, int index = 0, int total = 1,
                                   const std::string &targetExt = std::string());

// Renames every path and flags invalid names and case-insensitive duplicates.
// `aggregateError` receives the first problem found (empty when all are valid).
std::vector<BatchRenameResult> validateBatchRename(const std::vector<std::string> &filePaths,
                                                   const BatchRenameOptions &options,
                                                   std::string *aggregateError = nullptr,
                                                   const std::string &targetExt = std::string());

// QSettings persistence under `group` (e.g. "batchRename").
void saveBatchRenameSettings(const BatchRenameOptions &options,
                             const std::string &group = "batchRename");
BatchRenameOptions loadBatchRenameSettings(const std::string &group = "batchRename");

} // namespace mviewer::core
