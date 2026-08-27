#include "application/CommandLine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    bool ok = true;
    const auto check = [&ok](bool condition, const char *message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ok = false;
        }
    };

    check(!mviewer::application::isPositionalOpenArgument(QStringLiteral("--selftest")),
          "long options are not positional files");
    check(!mviewer::application::isPositionalOpenArgument(QStringLiteral("-q")),
          "short options are not positional files");
    check(mviewer::application::isPositionalOpenArgument(QStringLiteral("image.jpg")),
          "relative image paths are positional files");
    check(mviewer::application::isPositionalOpenArgument(QStringLiteral("/home/user/image.jpg")) ==
#ifdef Q_OS_WIN
              false,
#else
              true,
#endif
          "Unix absolute paths remain openable on Unix and slash switches stay reserved on Windows");
    check(mviewer::application::isPositionalOpenArgument(QStringLiteral("C:/images/image.jpg")),
          "Windows drive paths are positional files");
    check(mviewer::application::isPositionalOpenArgument(QStringLiteral("\\\\server\\share\\image.jpg")),
          "Windows UNC paths are positional files");

    QTemporaryDir fixture;
    check(fixture.isValid(), "temporary external-open fixture is available");
    if (fixture.isValid())
    {
        const QDir root(fixture.path());
        const QString unicodeDir = root.filePath(QStringLiteral("中文 空格 😀"));
        check(QDir().mkpath(unicodeDir), "Unicode directory fixture is created");
        const QString image = QDir(unicodeDir).filePath(QStringLiteral("sample image.PNG"));
        const QString workspace = root.filePath(QStringLiteral("session.MVWS"));
        const QString project = root.filePath(QStringLiteral("analysis.MVPROJ"));
        const QString text = root.filePath(QStringLiteral("notes.txt"));
        for (const QString &path : {image, workspace, project, text})
        {
            QFile file(path);
            check(file.open(QIODevice::WriteOnly), "external-open fixture file is created");
        }

        const auto imageTarget = mviewer::application::classifyExternalOpenTarget(image);
        check(imageTarget.kind == mviewer::application::ExternalOpenKind::Image,
              "supported image is classified as an image");
        const auto dirTarget = mviewer::application::classifyExternalOpenTarget(unicodeDir);
        check(dirTarget.kind == mviewer::application::ExternalOpenKind::Directory,
              "Unicode directory is classified as a directory");
        const auto workspaceTarget =
            mviewer::application::classifyExternalOpenTarget(workspace);
        check(workspaceTarget.kind == mviewer::application::ExternalOpenKind::Workspace,
              ".mvws is classified as a workspace");
        const auto projectTarget = mviewer::application::classifyExternalOpenTarget(project);
        check(projectTarget.kind == mviewer::application::ExternalOpenKind::Project,
              ".mvproj is classified as a project");
        const auto unsupportedTarget =
            mviewer::application::classifyExternalOpenTarget(text);
        check(!unsupportedTarget.isValid() && !unsupportedTarget.error.isEmpty(),
              "unsupported regular files are rejected");
        const auto missingTarget = mviewer::application::classifyExternalOpenTarget(
            root.filePath(QStringLiteral("missing.jpg")));
        check(!missingTarget.isValid() && !missingTarget.error.isEmpty(),
              "missing paths are rejected");

        const auto quotedPlan = mviewer::application::planExternalOpen(
            {QStringLiteral("\"") + image + QStringLiteral("\"")});
        check(quotedPlan.isValid() && quotedPlan.kind ==
                  mviewer::application::ExternalOpenKind::Image &&
                  quotedPlan.paths.front() == QFileInfo(image).absoluteFilePath(),
              "quoted paths with spaces normalize to the same image");

        const auto comparePlan = mviewer::application::planExternalOpen({image, image});
        check(comparePlan.isValid() && comparePlan.isCompare() && comparePlan.paths.size() == 2,
              "multiple supported images plan a compare open");
        const auto mixedPlan = mviewer::application::planExternalOpen({unicodeDir, image});
        check(!mixedPlan.isValid() && !mixedPlan.error.isEmpty(),
              "mixed directory and image targets are rejected atomically");
        const auto workspaceMultiPlan =
            mviewer::application::planExternalOpen({workspace, image});
        check(!workspaceMultiPlan.isValid(),
              "workspace plus another target is rejected atomically");
    }

    return ok ? 0 : 1;
}
