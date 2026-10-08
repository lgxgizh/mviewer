#include "core/batch/BatchRename.h"

#include "core/filesystem/Utf8Path.h"
#include "core/image/ImageTransform.h"

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSettings>
#include <QString>

#include <filesystem>
#include <utility>

namespace mviewer::core
{

namespace
{

const char *const kInvalidRegexPrefix = "正则表达式无效：";

// Compiles the find/replace options once so a whole file list can be renamed
// without rebuilding the regular expression per file.
class FindReplacer
{
  public:
    explicit FindReplacer(const BatchRenameOptions &options)
        : m_find(QString::fromStdString(options.find)),
          m_replace(QString::fromStdString(options.replace)), m_useRegex(options.useRegex),
          m_caseSensitivity(options.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive)
    {
        if (!m_useRegex || m_find.isEmpty())
            return;
        QRegularExpression::PatternOptions patternOptions = QRegularExpression::NoPatternOption;
        if (!options.caseSensitive)
            patternOptions |= QRegularExpression::CaseInsensitiveOption;
        m_regex = QRegularExpression(m_find, patternOptions);
    }

    QString errorString() const
    {
        if (!m_useRegex || m_find.isEmpty() || m_regex.isValid())
            return {};
        return m_regex.errorString();
    }

    QString apply(const QString &baseName) const
    {
        if (m_find.isEmpty() || !errorString().isEmpty())
            return baseName;
        QString result = baseName;
        if (m_useRegex)
            result.replace(m_regex, m_replace);
        else
            result.replace(m_find, m_replace, m_caseSensitivity);
        return result;
    }

  private:
    QString m_find;
    QString m_replace;
    bool m_useRegex = false;
    Qt::CaseSensitivity m_caseSensitivity = Qt::CaseInsensitive;
    QRegularExpression m_regex;
};

bool isReservedWindowsStem(const QString &fileName)
{
    static const QRegularExpression reserved(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$"),
        QRegularExpression::CaseInsensitiveOption);
    const qsizetype dot = fileName.indexOf(QLatin1Char('.'));
    if (dot == 0)
        return false;
    return reserved.match(dot > 0 ? fileName.left(dot) : fileName).hasMatch();
}

bool hasForbiddenCharacter(const QString &name)
{
    for (const QChar ch : name)
    {
        if (ch.unicode() < 32 || QStringLiteral("<>:\"|?*\\/").contains(ch))
            return true;
    }
    return false;
}

std::string withExtension(const std::string &name, const std::string &ext)
{
    return ext.empty() ? name : name + "." + ext;
}

// Problems in the replaced stem itself: a separator would otherwise be dropped
// silently by the rename pattern, and an empty stem leaves only ".ext".
std::string stemError(const QString &stem)
{
    if (stem.isEmpty())
        return "替换后的文件名为空。";
    if (hasForbiddenCharacter(stem))
        return "替换结果包含 Windows 不允许的字符（\\ / : * ? \" < > |）。";
    return {};
}

std::string composeName(const QString &stem, const std::string &ext,
                        const BatchRenameOptions &options, int index, int total)
{
    const std::string base = stem.toStdString();
    if (options.pattern.empty())
        return withExtension(base, ext);
    return withExtension(applyRenamePattern(options.pattern, base, ext, index, total), ext);
}

BatchRenameResult renameOne(const std::string &originalPath, const FindReplacer &replacer,
                            const BatchRenameOptions &options, int index, int total,
                            const std::string &targetExt)
{
    BatchRenameResult result;
    result.originalPath = originalPath;
    const std::filesystem::path path = pathFromUtf8(originalPath);
    const QString stem = QString::fromStdString(pathToUtf8(path.stem()));
    std::string ext = targetExt;
    if (ext.empty())
    {
        ext = pathToUtf8(path.extension());
        if (!ext.empty() && ext.front() == '.')
            ext.erase(0, 1);
    }
    const QString regexError = replacer.errorString();
    if (!regexError.isEmpty())
    {
        result.valid = false;
        result.errorMessage = kInvalidRegexPrefix + regexError.toStdString();
        result.newName = withExtension(stem.toStdString(), ext);
        return result;
    }
    const QString replaced = replacer.apply(stem);
    result.newName = composeName(replaced, ext, options, index, total);
    result.errorMessage = options.find.empty() ? std::string() : stemError(replaced);
    if (result.errorMessage.empty())
        result.errorMessage = fileNameError(result.newName);
    result.valid = result.errorMessage.empty();
    return result;
}

void markDuplicates(std::vector<BatchRenameResult> &results)
{
    QHash<QString, size_t> seen;
    for (size_t i = 0; i < results.size(); ++i)
    {
        const QString key = QString::fromStdString(results[i].newName).toCaseFolded();
        const auto it = seen.constFind(key);
        if (it == seen.constEnd())
        {
            seen.insert(key, i);
            continue;
        }
        const std::string message = "存在重复的目标文件名：" + results[i].newName;
        for (const size_t index : {it.value(), i})
        {
            if (!results[index].valid)
                continue;
            results[index].valid = false;
            results[index].errorMessage = message;
        }
    }
}

QString settingsKey(const std::string &group, const char *name)
{
    return QString::fromStdString(group) + QLatin1Char('/') + QLatin1String(name);
}

} // namespace

std::string applyFindReplace(const std::string &baseName, const std::string &find,
                             const std::string &replace, bool useRegex, bool caseSensitive,
                             std::string *errorMessage)
{
    BatchRenameOptions options;
    options.find = find;
    options.replace = replace;
    options.useRegex = useRegex;
    options.caseSensitive = caseSensitive;
    const FindReplacer replacer(options);
    const QString error = replacer.errorString();
    if (!error.isEmpty())
    {
        if (errorMessage)
            *errorMessage = error.toStdString();
        return baseName;
    }
    return replacer.apply(QString::fromStdString(baseName)).toStdString();
}

std::string findReplaceError(const BatchRenameOptions &options)
{
    const QString error = FindReplacer(options).errorString();
    return error.isEmpty() ? std::string() : kInvalidRegexPrefix + error.toStdString();
}

std::string fileNameError(const std::string &fileName)
{
    const QString name = QString::fromStdString(fileName);
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return "文件名不能为空。";
    if (name.size() > 255)
        return "文件名不能超过 255 个字符。";
    if (name != name.trimmed() || name.endsWith(QLatin1Char('.')))
        return "文件名不能以空格或句点开头或结尾。";
    if (hasForbiddenCharacter(name))
        return "文件名包含 Windows 不允许的字符（\\ / : * ? \" < > |）。";
    if (isReservedWindowsStem(name))
        return "文件名是 Windows 保留设备名。";
    if (QFileInfo(name).fileName() != name)
        return "文件名不能包含路径。";
    return {};
}

std::string composeRenamedFileName(const std::string &baseName, const std::string &ext,
                                   const BatchRenameOptions &options, int index, int total)
{
    const FindReplacer replacer(options);
    return composeName(replacer.apply(QString::fromStdString(baseName)), ext, options, index,
                       total);
}

BatchRenameResult applyBatchRename(const std::string &originalPath,
                                   const BatchRenameOptions &options, int index, int total,
                                   const std::string &targetExt)
{
    return renameOne(originalPath, FindReplacer(options), options, index, total, targetExt);
}

std::vector<BatchRenameResult> validateBatchRename(const std::vector<std::string> &filePaths,
                                                   const BatchRenameOptions &options,
                                                   std::string *aggregateError,
                                                   const std::string &targetExt)
{
    const FindReplacer replacer(options);
    const int total = static_cast<int>(filePaths.size());
    std::vector<BatchRenameResult> results;
    results.reserve(filePaths.size());
    for (int i = 0; i < total; ++i)
        results.push_back(
            renameOne(filePaths[static_cast<size_t>(i)], replacer, options, i, total, targetExt));
    markDuplicates(results);

    std::string firstError = findReplaceError(options);
    for (const BatchRenameResult &result : results)
    {
        if (!firstError.empty())
            break;
        firstError = result.errorMessage;
    }
    if (aggregateError)
        *aggregateError = firstError;
    return results;
}

void saveBatchRenameSettings(const BatchRenameOptions &options, const std::string &group)
{
    QSettings settings;
    settings.setValue(settingsKey(group, "pattern"), QString::fromStdString(options.pattern));
    settings.setValue(settingsKey(group, "find"), QString::fromStdString(options.find));
    settings.setValue(settingsKey(group, "replace"), QString::fromStdString(options.replace));
    settings.setValue(settingsKey(group, "useRegex"), options.useRegex);
    settings.setValue(settingsKey(group, "caseSensitive"), options.caseSensitive);
    settings.sync();
}

BatchRenameOptions loadBatchRenameSettings(const std::string &group)
{
    const QSettings settings;
    BatchRenameOptions options;
    options.pattern = settings.value(settingsKey(group, "pattern")).toString().toStdString();
    options.find = settings.value(settingsKey(group, "find")).toString().toStdString();
    options.replace = settings.value(settingsKey(group, "replace")).toString().toStdString();
    options.useRegex = settings.value(settingsKey(group, "useRegex"), false).toBool();
    options.caseSensitive = settings.value(settingsKey(group, "caseSensitive"), false).toBool();
    return options;
}

} // namespace mviewer::core
