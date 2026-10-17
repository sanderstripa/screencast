from pathlib import Path
import sys
from PIL import Image
import numpy as np

root=Path(sys.argv[1])
for page in range(4):
    for theme,expected in [('light',(247,247,245)),('dark',(12,12,13))]:
        images=[]
        for language in ['ru','en']:
            a=np.array(Image.open(root/f'setup-{language}-{theme}-{page}.png').convert('RGB'))
            assert a.shape==(484,680,3)
            assert np.max(abs(a[8,8].astype(int)-expected))<=1
            assert np.std(a[28:140,284:396])>30, 'Application icon absent'
            assert np.std(a[430:468,528:660])>15, 'Primary action absent'
            images.append(a)
        assert np.mean(abs(images[0].astype(float)-images[1]))>.05, 'Language does not change the wizard'
for theme,expected in [('light',(247,247,245)),('dark',(12,12,13))]:
    def blue_count(a):
        return np.count_nonzero((a[:,:,2].astype(int)>a[:,:,0].astype(int)+60)&(a[:,:,1].astype(int)>a[:,:,0].astype(int)+30))
    on=np.array(Image.open(root/f'setup-ru-{theme}-1.png').convert('RGB'))
    off=np.array(Image.open(root/f'setup-ru-{theme}-1-off.png').convert('RGB'))
    ink=np.array((237,237,238) if theme=='dark' else (23,26,29))
    def ink_count(a):return np.count_nonzero(np.max(abs(a.astype(int)-ink),axis=2)<8)
    assert ink_count(on[288:354,36:75])>300, 'Enabled shortcuts lack monochrome contrast'
    assert ink_count(off[288:354,36:75])<40, 'Disabled shortcuts still show active fill'
    assert np.max(abs(on[360:390,36:624].astype(int)-expected))<=1, 'Duplicate launch option remains on folder page'
    finish=np.array(Image.open(root/f'setup-ru-{theme}-3.png').convert('RGB'))
    finish_off=np.array(Image.open(root/f'setup-ru-{theme}-3-off.png').convert('RGB'))
    assert np.max(abs(finish[430:468,396:516].astype(int)-expected))<=1, 'Cancel remains on completion page'
    # Restrict the check to the circle: ClearType text has blue subpixel fringes.
    assert ink_count(finish[322:352,150:210])>150 and ink_count(finish_off[322:352,150:210])<40, 'Completion check does not visibly change'
print('All four installer pages rendered in RU/EN and light/dark themes')
