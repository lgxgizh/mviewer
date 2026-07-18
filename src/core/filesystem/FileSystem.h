#pragma once

#include <string>
#include <vector>

// 文件系统层：扫描目录、枚举图片文件。后续可加变更监听（QFileSystemWatcher）。
class FileSystem
{
  public:
    static std::vector<std::string> imageFilters();

    // 列出目录下的图片（按文件名），超限截断。
    // Default cap raised from 2000 -> 100000 so directories with >2000 images
    // (e.g. the 10000-image benchmark corpus) are no longer silently truncated.
    // An explicit `max` is still honored (pass 0 for "no limit").
    static std::vector<std::string> listImages(const std::string &dir, int max = 100000);

    // 判断路径是否为支持的图像文件
    static bool isImage(const std::string &path);
};
