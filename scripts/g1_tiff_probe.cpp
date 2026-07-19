// G1 proof: decode a TIFF on a "clean Windows" (only the staged Qt dir on PATH).
// Links against mviewer_core (which exposes Decoder::decodeFull).
#include "core/image/Decoder.h"
#include "core/image/ImageBuffer.h"
#include <cstdio>
#include <string>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: g1_tiff_probe <file.tif>\n");
        return 2;
    }
    const std::string path = argv[1];
    const ImageData img = Decoder::decodeFull(path);
    if (img.isNull())
    {
        printf("G1_PROBE_FAIL: decode returned null ImageData for %s\n", path.c_str());
        return 1;
    }
    printf("G1_PROBE_OK: %s -> %d x %d (channels=%d)\n",
           path.c_str(), img.width, img.height, img.channelsPerPixel());
    return 0;
}
