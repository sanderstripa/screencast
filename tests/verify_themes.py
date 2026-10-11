from pathlib import Path
import sys
from PIL import Image
import numpy as np

root = Path(sys.argv[1])
images = []
for name, expected in (("light", (247, 247, 245)), ("dark", (11, 11, 12))):
    image = np.asarray(Image.open(root / f"theme-{name}.png").convert("RGB"))
    assert image.shape[0] >= 600 and image.shape[1] >= 950, 'Compact window must fit on a 1024x768 desktop'
    assert np.max(abs(image[8, 8].astype(int) - np.array(expected))) <= 1
    assert np.unique(image.reshape(-1, 3), axis=0).shape[0] > 30, "Empty or blank UI snapshot"
    # Text must exist in the title and in the audio section, not merely a background fill.
    for area in (image[24:50, 30:300], image[100:122, 336:460]):
        assert np.max(area) - np.min(area) > 60, f"Missing text in {name} theme"
    images.append(image)
    print(f"{name}: palette, non-empty controls and text checked")
assert np.mean(abs(images[0].astype(float) - images[1].astype(float))) > 150
