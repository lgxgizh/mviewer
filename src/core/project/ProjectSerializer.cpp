#include "core/project/ProjectSerializer.h"

#include "core/workspace/WorkspaceSerializer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace mviewer
{
namespace core
{

std::string serializeProject(const domain::Project &p)
{
    QJsonObject obj;
    obj["schema"] = "mviewer.project";
    obj["version"] = 1;
    const auto utf8 = [](const std::string &value)
    {
        return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
    };
    obj["name"] = utf8(p.name);
    obj["filePath"] = utf8(p.filePath);
    obj["createdIso"] = utf8(p.createdIso);
    obj["modifiedIso"] = utf8(p.modifiedIso);
    obj["appVersion"] = utf8(p.appVersion);
    obj["reviewNotes"] = utf8(p.reviewNotes);
    obj["analyzerPipelineJson"] = utf8(p.analyzerPipelineJson);
    obj["exportConfigJson"] = utf8(p.exportConfigJson);
    obj["benchmarkBaselineJson"] = utf8(p.benchmarkBaselineJson);

    QJsonArray roots;
    for (const auto &r : p.datasetRoots)
        roots.append(utf8(r));
    obj["datasetRoots"] = roots;

    QJsonArray pipe;
    for (const auto &a : p.analyzerPipeline)
        pipe.append(utf8(a));
    obj["analyzerPipeline"] = pipe;

    // Embed the workspace as a base64 string so the .mvproj is fully
    // self-contained and we never parse JSON-inside-JSON (which is fragile for
    // arbitrary image paths / metadata). deserializeWorkspace does the real
    // workspace parsing on the decoded payload.
    const std::string wsJson = serializeWorkspace(p.workspace);
    obj["workspaceB64"] = QString::fromLatin1(QByteArray::fromStdString(wsJson).toBase64());

    QJsonDocument doc(obj);
    return doc.toJson(QJsonDocument::Indented).toStdString();
}

bool deserializeProject(const std::string &json, domain::Project &out)
{
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
    if (!doc.isObject())
        return false;
    const QJsonObject obj = doc.object();

    if (!obj.contains("workspaceB64") || !obj["workspaceB64"].isString())
        return false;
    const QByteArray wsJson = QByteArray::fromBase64(obj["workspaceB64"].toString().toLatin1());
    const auto maybeWs = deserializeWorkspace(std::string(wsJson.constData(), wsJson.size()));
    if (!maybeWs)
        return false;
    out.workspace = std::move(*maybeWs);

    auto str = [&](const char *k, std::string &dst)
    {
        if (obj.contains(k) && obj[k].isString())
            dst = obj[k].toString().toUtf8().toStdString();
    };
    str("name", out.name);
    str("filePath", out.filePath);
    str("createdIso", out.createdIso);
    str("modifiedIso", out.modifiedIso);
    str("appVersion", out.appVersion);
    str("reviewNotes", out.reviewNotes);
    str("analyzerPipelineJson", out.analyzerPipelineJson);
    str("exportConfigJson", out.exportConfigJson);
    str("benchmarkBaselineJson", out.benchmarkBaselineJson);

    out.datasetRoots.clear();
    if (obj.contains("datasetRoots") && obj["datasetRoots"].isArray())
        for (const QJsonValue &v : obj["datasetRoots"].toArray())
            if (v.isString())
                out.datasetRoots.push_back(v.toString().toUtf8().toStdString());

    out.analyzerPipeline.clear();
    if (obj.contains("analyzerPipeline") && obj["analyzerPipeline"].isArray())
        for (const QJsonValue &v : obj["analyzerPipeline"].toArray())
            if (v.isString())
                out.analyzerPipeline.push_back(v.toString().toUtf8().toStdString());

    return true;
}

} // namespace core
} // namespace mviewer
