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
    focused=np.array(Image.open(root/f'theme-{name}-mouse-focus.png').convert('RGB'))
    assert np.array_equal(image,focused), 'A focused control adds an unwanted outline'
    dropdown=np.array(Image.open(root/f'theme-{name}-dropdown.png').convert('RGB'))
    assert np.max(abs(dropdown[168, 630].astype(int)-np.array(expected)))<=1, 'Dropdown uses the system menu background'
    assert not np.array_equal(image[160:274,336:636],dropdown[160:274,336:636]), 'Dropdown was not rendered'
    assert np.unique(dropdown[166:268,372:600].reshape(-1,3),axis=0).shape[0]>30, 'Dropdown text or selection missing'
    print(f"{name}: palette, non-empty controls and text checked")
assert np.mean(abs(images[0].astype(float) - images[1].astype(float))) > 150
icon=Image.open(Path('assets/Screencast.ico'))
for size in icon.ico.sizes():
    alpha=np.asarray(icon.ico.getimage(size).convert('RGBA'))[:,:,3]
    y,x=np.where(alpha>128)
    assert x.max()-x.min()+1>=int(size[0]*.9) and y.max()-y.min()+1>=int(size[1]*.9), 'Icon artwork leaves excessive transparent padding'
print('All focused controls match unfocused rendering; icon artwork fills every ICO size')
