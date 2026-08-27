#include "application/CommandLine.h"

#include <QCoreApplication>

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

    return ok ? 0 : 1;
}
