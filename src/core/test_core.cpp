// M2 core self-test: verifies Decoder / ImageCache / TaskScheduler / ImageFrame
// actually work. Usage: built standalone, linked with the core objects + Qt6, run headless.
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

    // Use a relative path to testdata/golden so this works on any machine and
    // in CI.  For ad-hoc runs from the build directory pass the image path as
    // the first argument.
    const std::string p =
        (argc > 1) ? std::string(argv[1]) : std::string("../testdata/golden/pixnio-6000x4000.jpg");

    // 1) Decoder decodes at thumbnail size
    ImageData thumb = Decoder::decodeScaled(p, 140);
    printf("DECODE_SCALED=%d %dx%d\n", !thumb.isNull(), thumb.width, thumb.height);

    // 2) Cache write and read
    ImageCache::instance().put(ImageCache::Thumbnail, p, thumb);
    ImageData back;
    bool ok = ImageCache::instance().get(ImageCache::Thumbnail, p, back);
    printf("CACHE_GET=%d %dx%d\n", ok, back.width, back.height);

    // 3) Scheduler: full-image decode in the background + callback
    ImageData full;
    bool done = false;
    TaskScheduler::instance().submit(
        TaskScheduler::DecodePool, [&]() { full = Decoder::decodeFull(p); },
        [&]()
        {
            printf("SCHED_DECODE=%d %dx%d\n", !full.isNull(), full.width, full.height);
            // 4) ImageFrame statistics
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
