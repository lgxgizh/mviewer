// M2 Core 自测：验证 Decoder / ImageCache / TaskScheduler / ImageFrame
// 真正工作。 用法：独立编译，链接 core 的 .obj + Qt6，跑 headless。
#include "core/image/Decoder.h"
#include "core/image/ImageCache.h"
#include "core/image/ImageFrame.h"
#include "core/scheduler/TaskScheduler.h"

#include <QCoreApplication>
#include <QImage>
#include <QTimer>
#include <cstdio>
#include <string>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const std::string p = "D:/photos/pixnio-6000x4000.jpg";

    // 1) Decoder 按缩略图尺寸解码
    ImageData thumb = Decoder::decodeScaled(p, 140);
    printf("DECODE_SCALED=%d %dx%d\n", !thumb.isNull(), thumb.width, thumb.height);

    // 2) 缓存写入读取
    ImageCache::instance().put(ImageCache::Thumbnail, p, thumb);
    ImageData back;
    bool ok = ImageCache::instance().get(ImageCache::Thumbnail, p, back);
    printf("CACHE_GET=%d %dx%d\n", ok, back.width, back.height);

    // 3) 调度器：后台解码全图 + 回调
    ImageData full;
    bool done = false;
    TaskScheduler::instance().submit(
        TaskScheduler::DecodePool, [&]() { full = Decoder::decodeFull(p); },
        [&]()
        {
            printf("SCHED_DECODE=%d %dx%d\n", !full.isNull(), full.width, full.height);
            // 4) ImageFrame 统计
            ImageFrame frame = ImageFrame::create(p, full);
            frame.computeHistogram();
            double r, g, b;
            frame.rgbMeans(r, g, b);
            printf("FRAMEOBJ_LUM=%.1f RGB=%.0f,%.0f,%.0f\n", frame.luminanceMean(), r, g, b);
            done = true;
            app.quit();
        });

    QTimer::singleShot(8000, &app,
                       [&]()
                       {
                           if (!done)
                           {
                               printf("TIMEOUT\n");
                               app.quit();
                           }
                       });
    return app.exec();
}
