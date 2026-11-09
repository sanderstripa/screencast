from pathlib import Path
import sys
from PIL import Image
import numpy as np
root=Path(sys.argv[1]);images=[]
for name,expected in (("light",(255,255,255)),("dark",(8,9,11))):
    load=lambda suffix: np.asarray(Image.open(root/f"theme-{name}{suffix}.png").convert("RGB"))
    image=load("");images.append(image)
    assert image.shape[0]>=600 and image.shape[1]>=950
    assert np.max(abs(image[8,8].astype(int)-expected))<=1
    for area in (image[24:50,30:300],image[100:122,336:460]):
        assert np.ptp(area)>60,"Missing title/column text"
    for mode in ("mouse-focus","keyboard-focus"):
        assert np.array_equal(image,load("-"+mode)),"Unexpected focus decoration"
    # No gear; the space it occupied is now an unadorned titlebar.
    assert np.max(abs(image[16:52,848:884].astype(int)-expected))<=1,"Gear remains"
    hover=load("-window-hover");start=load("-window-hover-start")
    assert not np.array_equal(hover,start),"Hover transition is not animated"
    x=image.shape[1]-54
    rose=np.array((84,36,47) if name=="dark" else (246,209,217))
    assert np.max(abs(hover[21,x+16].astype(int)-rose))<=2,"Close hover does not use muted rose"
    stable=np.ones(image.shape[:2],bool)
    stable[16:48,image.shape[1]-94:image.shape[1]-22]=False
    assert np.array_equal(image[stable],hover[stable]),"Hover changes unrelated controls"
    settings=load("-settings");settings_en=load("-settings-en")
    assert settings.shape==image.shape
    assert np.array_equal(settings,load("-settings-focus"))
    for top,bottom,left,right in ((18,48,24,300),(126,162,306,776),(174,210,306,616),(292,328,766,952),(422,458,766,952),(494,522,420,580)):
        assert np.ptp(settings[top:bottom,left:right])>25,"Missing settings content"
    assert not np.array_equal(settings[82:480,24:952],settings_en[82:480,24:952]),"Settings translation missing"
    assert not np.array_equal(image[100:522,24:976],load("-en")[100:522,24:976]),"Main translation missing"
    # The lower visible working interface is blurred and dimmed under a full-width top sheet.
    assert not np.array_equal(image[560:616,24:976],settings[560:616,24:976]),"Backdrop absent"
    assert np.std(settings[560:616,24:976])>1,"Backdrop is a blank fill"
    assert np.max(abs(settings[8,8].astype(int)-expected))<=1,"Sheet not attached to top"
    dropdown=load("-dropdown")
    assert not np.array_equal(image[160:274,336:636],dropdown[160:274,336:636]),"Dropdown missing"
    print(f"{name}: palette, three columns, gear removal, rose hover animation, settings and blurred backdrop verified")
assert np.mean(abs(images[0].astype(float)-images[1].astype(float)))>150
icon=Image.open(Path('assets/Screencast.ico'))
required={16,18,20,22,24,28,30,32,36,40,42,44,48,50,54,56,60,64,72,80,84,96,112,120,128,144,160,180,192,216,224,240,256}
assert {(n,n) for n in required} <= icon.ico.sizes(), 'Missing native icon size; Windows would have to resample another entry'
assert Path('assets/Screencast-shell-0.4.7.ico').read_bytes()==Path('assets/Screencast.ico').read_bytes(), 'Shortcuts and executable use different icon artwork'
for alias in Path('assets').glob('Screencast-shell-*.ico'):
    assert alias.read_bytes()==Path('assets/Screencast.ico').read_bytes(), f'Stale legacy branding: {alias}'
svg=Path('assets/Screencast.svg').read_text(encoding='utf-8-sig')
assert '<image' not in svg and 'filter' not in svg, 'Vector source must not embed a bitmap or blur'
assert Image.open('assets/Screencast-master.png').size==(4096,4096), 'Native large export is missing'
for size in icon.ico.sizes():
    frame=np.asarray(icon.ico.getimage(size).convert('RGBA'))
    assert np.array_equal(frame,np.asarray(Image.open(f'assets/icon-sizes/{size[0]}.png').convert('RGBA'))), 'ICO differs from native vector render'
    alpha=frame[:,:,3]
    y,x=np.where(alpha>128)
    assert x.max()-x.min()+1>=int(size[0]*.9) and y.max()-y.min()+1>=int(size[1]*.9), 'Icon artwork leaves excessive transparent padding'
    pixels=np.asarray(icon.ico.getimage(size).convert('RGBA')).astype(int)
    assert alpha[0,0]==0 and alpha[-1,-1]==0, 'Outer white tile/background must be absent'
    center=pixels[size[0]//2,size[0]//2]
    assert center[3]>240 and center[0]>center[1]+25, 'Coral recording dot must remain visible'
    opaque=pixels[alpha>200,:3]
    assert np.count_nonzero((opaque[:,2]>opaque[:,0]+8)&(opaque[:,0]>opaque[:,1]+3))>=size[0]*.3, 'Lavender screen frame is missing'
    assert np.count_nonzero((opaque[:,0]>130)&(opaque[:,0]-opaque[:,1]>25))>=1, 'Recording dot is missing'
print('All focused controls match unfocused rendering; icon artwork fills every ICO size')
