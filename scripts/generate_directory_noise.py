"""Generate synthetic RGBA8 noise/ICC PNG; no user originals or binary repo asset."""
import random
import sys
from pathlib import Path
from PIL import Image, ImageCms

rng = random.Random(178)
size = (3838, 2156)  # Assumed representative height, not confirmed original.
image = Image.frombytes("RGBA", size, rng.randbytes(size[0] * size[1] * 4))
profile = ImageCms.ImageCmsProfile(ImageCms.createProfile("sRGB")).tobytes()
output = Path(sys.argv[1])
output.parent.mkdir(parents=True, exist_ok=True)
image.save(output, icc_profile=profile, compress_level=1)
print(f"Generated {output}: {image.mode} {size}, {output.stat().st_size} bytes")
