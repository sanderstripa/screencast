from pathlib import Path
import sys,json,hashlib,math
from PIL import Image
import numpy as np

root=Path(sys.argv[1]);repo=Path(__file__).resolve().parents[1]
generated=repo/'installer/generated'
manifest=json.loads((generated/'manifest.json').read_text())
assert manifest['version']=='0.4.1'
for name,digest in manifest['sources'].items():
    data=(repo/'installer/visual-review'/name).read_bytes()
    if name.endswith(('.html','.css','.js')):data=data.replace(b'\r\n',b'\n')
    assert hashlib.sha256(data).hexdigest()==digest,f'Approved installer source changed: {name}'
for asset in manifest['assets']:
    assert hashlib.sha256((generated/asset['file']).read_bytes()).hexdigest()==asset['sha256'],f'Native presentation asset changed: {asset["file"]}'

reference_source=repo/'installer/connect-reference'
pin=json.loads((reference_source/'source.json').read_text())
for name,digest in pin['files'].items():
    data=(reference_source/name).read_bytes().replace(b'\r\n',b'\n')
    assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()==digest,f'Pinned CONNECT source differs: {name}'
assert (repo/'installer/visual-review/style.css').read_text().startswith((reference_source/'style.css').read_text()),'CONNECT CSS must be reused unchanged'
font=(repo/'installer/visual-review/assets/Montserrat.ttf').read_bytes()
assert hashlib.sha1(b'blob '+str(len(font)).encode()+b'\0'+font).hexdigest()==pin['fontGitBlob'],'Montserrat differs from CONNECT'

def bounds(language,page,control):
    return tuple((math.floor if i<2 else math.ceil)(v) for i,v in enumerate(manifest['layout'][f'{language}-{page}'][str(control)]))

for theme,expected in [('light',(242,240,235)),('dark',(32,33,34))]:
    for page in range(4):
        images=[]
        for language,locale in enumerate(('ru','en')):
            frame=np.asarray(Image.open(root/f'setup-{locale}-{theme}-{page}.png').convert('RGB'))
            assert frame.shape==(540,840,3),'Installer footprint differs from approved 840x540 layout'
            base=Image.open(generated/f'page-96-{language}-{int(theme=="dark")}-{page}.png').convert('RGBA')
            reference=np.asarray(Image.alpha_composite(Image.new('RGBA',base.size,(*expected,255)),base).convert('RGB'))
            stable=np.ones(frame.shape[:2],dtype=bool)
            if page==1:
                l,t,r,b=bounds(language,page,14);stable[t:b,l:r]=False
                assert np.std(frame[t+7:b-7,l+10:r-10])>5,'Editable installation path is blank'
            if page==2:
                l,t,r,b=bounds(language,page,15);stable[max(0,t-3):b+3,max(0,l-3):r+3]=False
                assert np.std(frame[t:b,l:r])>15,'Live installation progress is missing'
            delta=np.max(np.abs(frame.astype(int)-reference.astype(int)),axis=2)
            assert delta[stable].max()<=2,f'{locale}/{theme}/stage {page} differs from approved presentation: {delta[stable].max()}'
            assert np.max(abs(frame[10,10].astype(int)-expected))<=1
            assert np.std(frame[494:535,690:795])>5,'Application version footer missing'
            if page in (0,2):assert np.std(frame[100:350,400:790] if page==0 else frame[90:340,200:650])>15,'Approved lavender ribbon missing'
            images.append(frame)
        assert np.count_nonzero(np.any(images[0]!=images[1],axis=2))>30,'Language does not change the actual wizard'
    for page,controls in ((1,(5,6)),(3,(7,))):
        on=np.asarray(Image.open(root/f'setup-ru-{theme}-{page}.png').convert('RGB'))
        off=np.asarray(Image.open(root/f'setup-ru-{theme}-{page}-off.png').convert('RGB'))
        for control in controls:
            l,t,r,b=bounds(0,page,control)
            tile=np.asarray(Image.open(generated/f'off-96-0-{int(theme=="dark")}-{control}.png').convert('RGB'))
            assert np.max(abs(off[t:b,l:r].astype(int)-tile.astype(int)))<=2,'Unchecked switch differs from approved reference'
            assert np.count_nonzero(np.any(on[t:b,l:l+39]!=off[t:b,l:l+39],axis=2))>50,'Switch does not visibly change'
for page,controls in ((1,(8,14,4)),(3,(4,))):
    name='settings' if page==1 else 'finish'
    actual=np.asarray(Image.open(root/f'connect-actual-{name}.png').convert('RGB'))
    reference=np.asarray(Image.open(root/f'connect-reference-{name}.png').convert('RGB'))
    for control in controls:
        l,t,r,b=bounds(0,page,control)
        assert np.array_equal(actual[t:b,l:r],reference[t:b,l:r]),f'Actual WebView2 {name}/{control} pixels differ from original CONNECT component'
    if page==1:
        assert np.array_equal(actual[162:177,45:795],reference[162:177,45:795]),'Installation-folder label pixels differ from CONNECT'
print('PASS: live WebView2 installer matches CONNECT path label/input/browse/primary button pixels exactly; RU/EN light/dark screens, 0.4.1 footer, live progress/path/switches and all 224 reference resources verified.')
