from pathlib import Path
import sys
from PIL import Image
import numpy as np

root=Path(sys.argv[1])
for page in range(4):
    for theme,expected in [('light',(247,247,245)),('dark',(11,11,12))]:
        images=[]
        for language in ['ru','en']:
            a=np.array(Image.open(root/f'setup-{language}-{theme}-{page}.png').convert('RGB'))
            assert a.shape==(484,680,3)
            assert np.max(abs(a[8,8].astype(int)-expected))<=1
            assert np.std(a[28:140,284:396])>30, 'Application icon absent'
            assert np.std(a[430:468,528:660])>15, 'Primary action absent'
            images.append(a)
        assert np.mean(abs(images[0].astype(float)-images[1]))>.05, 'Language does not change the wizard'
print('All four installer pages rendered in RU/EN and light/dark themes')
