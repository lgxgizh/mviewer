#pragma once

#include <QObject>
#include <QImage>
#include <QString>

// 解码器：封装 QImageReader，支持
//  - 全分辨率解码
//  - 按目标尺寸缩小解码（缩略图/预览，省内存）
//  - 区域解码（后续做差异图/局部分析时用）
// 所有解码都在后台线程跑，这里不碰 UI。
class Decoder : public QObject
{
    Q_OBJECT

public:
    // 全图解码
    static QImage decodeFull(const QString &path);

    // 缩放到 maxEdge 以内的解码（保持比例）
    static QImage decodeScaled(const QString &path, int maxEdge);

    // 支持的图像后缀（与 UI 过滤保持一致）
    static QStringList supportedExtensions();
};
