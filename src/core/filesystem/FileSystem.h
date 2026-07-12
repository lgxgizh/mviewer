#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// 文件系统层：扫描目录、枚举图片文件。后续可加变更监听（QFileSystemWatcher）。
class FileSystem : public QObject
{
    Q_OBJECT

public:
    static QStringList imageFilters();

    // 列出目录下的图片（按文件名），超限截断。
    static QStringList listImages(const QString &dir,
                                  int max = 2000);

    // 判断路径是否为支持的图像文件
    static bool isImage(const QString &path);
};
