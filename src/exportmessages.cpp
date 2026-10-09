#include "exportmessages.h"

#include <QLatin1String>
#include <QString>

namespace mviewer::ui
{
namespace
{

struct ExportMessagePrefix
{
    const char *english;
    const char *chinese;
};

// Longer prefixes precede shorter ones. A prefix that ends in ": " already
// includes that separator in the Chinese text; the remainder is appended raw.
constexpr ExportMessagePrefix kExportMessagePrefixes[] = {
    {"duplicate destination conflict", "目标路径重复冲突"},
    {"source/destination conflict", "源文件与目标文件冲突"},
    {"no source image decoded", "没有解码成功的源图片"},
    {"explicit destination requires one source", "指定目标文件时只能有一个源文件"},
    {"cannot create output directory: ", "无法创建输出目录："},
    {"source directory enumeration failed: ", "源目录枚举失败："},
    {"contact sheet exceeds the compositing budget (", "联系表超出合成预算（"},
    {"staging memory budget exceeded", "暂存内存超出预算"},
    {"clipboard source decode failed", "剪贴板源图解码失败"},
    {"invalid path encoding: ", "路径编码无效："},
    {"output generation failed", "输出生成失败"},
    {"output commit failed: ", "输出写入失败："},
    {"clipboard image ready", "剪贴板图片已就绪"},
    {"no output directory", "没有输出目录"},
    {"unsupported format: ", "不支持的格式："},
    {"report write failed", "报告写入失败"},
    {"cancelled after ", "已取消，完成 "},
    {"no sources", "没有源文件"},
};

QString doneSummary(const mviewer::exportjob::ExportJobResult &result, const QString &message)
{
    QString text = QStringLiteral("导出完成：%1 / %2").arg(result.done).arg(result.total);
    if (result.failed > 0)
        text += QStringLiteral("，失败 %1 个").arg(result.failed);
    const qsizetype colon = message.indexOf(QStringLiteral(": "));
    if (colon >= 0)
        text += message.mid(colon);
    return text;
}

QString translateKnown(const QString &message)
{
    for (const ExportMessagePrefix &row : kExportMessagePrefixes)
    {
        const QLatin1String prefix(row.english);
        if (!message.startsWith(prefix))
            continue;
        QString text = QString::fromUtf8(row.chinese);
        if (prefix.size() < message.size())
            text += message.mid(prefix.size());
        return text;
    }
    return message;
}

} // namespace

QString exportResultMessageZh(const mviewer::exportjob::ExportJobResult &result)
{
    const QString message = QString::fromStdString(result.message);
    if (message.startsWith(QLatin1String("done ")))
        return doneSummary(result, message);
    return translateKnown(message);
}

} // namespace mviewer::ui
