from pathlib import Path
import sys
from PIL import Image
import numpy as np
import hashlib
import re

root = Path(sys.argv[1])
repo = Path(__file__).resolve().parents[1]
pixel_header = (repo/'src/vpn_theme_pixels.h').read_text()
theme_masks = {}
for icon in ('monitor','sun','moon'):
    svg = (repo/f'assets/connect-ui/{icon}.svg').read_text().replace('\r\n','\n')
    assert f'{icon} source SHA256 {hashlib.sha256(svg.encode()).hexdigest()}' in pixel_header
    mask = np.asarray(Image.open(repo/f'assets/connect-ui/{icon}-browser-13.png').convert('RGBA'))[:,:,3]
    native = np.array([int(v) for v in re.search(rf'{icon}13\[\]=\{{([^}}]+)\}}',pixel_header)[1].split(',')],dtype=np.uint8).reshape(13,13)
    assert np.array_equal(mask,native), 'Native theme mask differs from browser reference'
    theme_masks[icon] = mask.astype(float)/255
images = []
window_header=(repo/'src/vpn_window_pixels.h').read_text()
window_masks={}
for icon in ('minus','x'):
    svg=(repo/f'assets/connect-ui/{icon}.svg').read_text().replace('\r\n','\n')
    assert f'{icon} source SHA256 {hashlib.sha256(svg.encode()).hexdigest()}' in window_header
    for dpi in (96,120,144,168,192,240,288,384):
        mask=np.asarray(Image.open(repo/f'assets/connect-ui/{icon}-button-browser-{dpi}.png').convert('RGBA'))[:,:,3]
        native=np.array([int(v) for v in re.search(rf'{icon}{dpi}\[\]=\{{([^}}]+)\}}',window_header)[1].split(',')],dtype=np.uint8).reshape(mask.shape)
        assert np.array_equal(mask,native), f'{icon} native mask differs from VPN browser reference at {dpi} DPI'
        if dpi==96:window_masks[icon]=mask.astype(float)/255
for name, expected in (("light", (247, 247, 245)), ("dark", (12, 12, 13))):
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
    keyboard=np.array(Image.open(root/f'theme-{name}-keyboard-focus.png').convert('RGB'))
    assert np.array_equal(image,keyboard), 'Keyboard focus adds an unwanted highlight'
    hovered=np.array(Image.open(root/f'theme-{name}-window-hover.png').convert('RGB'))
    hover_start=np.array(Image.open(root/f'theme-{name}-window-hover-start.png').convert('RGB'))
    assert np.array_equal(hovered,hover_start), 'Window button hover must use settings icon foreground immediately, without a different tint ramp'
    for icon,left in (('minus',image.shape[1]-86),('x',image.shape[1]-50)):
        alpha=window_masks[icon][:,:,None]
        for frame,foreground in ((image,(129,133,140) if name=='dark' else (128,133,139)),(hovered,(237,237,238) if name=='dark' else (23,26,29))):
            reference=np.rint(alpha*np.array(foreground)+(1-alpha)*np.array(expected))
            error=np.max(np.abs(frame[16:46,left:left+28].astype(float)-reference))
            assert error<=2, f'{name} {icon} shape/size/centering/hover differs from VPN/settings reference: {error}'
    stable=np.ones(image.shape[:2],dtype=bool)
    for x in (image.shape[1]-86,image.shape[1]-50):stable[23:39,x+6:x+22]=False
    assert np.array_equal(image[stable],hovered[stable]), 'Minimize/close hover adds a background tile'
    settings=np.array(Image.open(root/f'theme-{name}-settings.png').convert('RGB'))
    settings_focus=np.array(Image.open(root/f'theme-{name}-settings-focus.png').convert('RGB'))
    assert np.array_equal(settings,settings_focus), 'Settings or theme buttons add a focus frame'
    # CONNECT's language chips sit above its icon-and-label theme row.
    assert np.std(settings[140:168,312:380])>12, 'RU/EN chips missing'
    for left in (24,144,264):
        tile=settings[176:208,left:left+116]
        assert np.std(tile)>12, 'CONNECT theme picker control missing'
    for icon,left,selected in (('monitor',24,False),('sun',144,name=='light'),('moon',264,name=='dark')):
        foreground=np.array((237,237,238) if name=='dark' else (23,26,29)) if selected else np.array((129,133,140) if name=='dark' else (128,133,139))
        background=np.array((26,27,30) if name=='dark' else (238,238,238)) if selected else np.array(expected)
        alpha=theme_masks[icon][:,:,None]
        reference=np.rint(alpha*foreground+(1-alpha)*background)
        errors=[np.max(np.abs(settings[y:y+13,x:x+13].astype(float)-reference)) for y in range(184,187) for x in range(left+5,left+55)]
        assert min(errors)<=2, f'{name} {icon} differs from CONNECT browser SVG pixels: {min(errors)}'
    gear=image[16:52,848:884].astype(int)
    bg=np.array(expected)
    for edge_area in (gear[:4],gear[-4:],gear[:,:4],gear[:,-4:]):
        assert np.max(abs(edge_area-bg))<=1, 'Settings icon has a background tile or frame'
    assert np.max(abs(image[34,866].astype(int)-bg))>50, 'Approved gear center dot is absent'
    assert np.max(abs(image[34,862].astype(int)-bg))<=1, 'Approved gear inner opening is not transparent'
    # Away from the track, a smooth circular thumb has antialiased edge coverage.
    # A stroked, pixel-stepped GDI ellipse has only solid foreground/background pixels.
    thumb=image[246:259,480:493].astype(int)
    ink=np.array((237,237,238) if name=='dark' else (23,26,29))
    edge=(np.max(abs(thumb-np.array(expected)),axis=2)>5)&(np.max(abs(thumb-ink),axis=2)>5)
    edge[5:8,:]=False
    assert np.count_nonzero(edge)>=8, 'Slider thumb lacks smooth circle edges'
    settings_en=np.array(Image.open(root/f'theme-{name}-settings-en.png').convert('RGB'))
    main_en=np.array(Image.open(root/f'theme-{name}-en.png').convert('RGB'))
    assert settings.shape==image.shape==main_en.shape, 'Settings resized the application'
    assert np.max(abs(settings[8,8].astype(int)-np.array(expected)))<=1, 'Settings changed the palette'
    assert np.std(settings[128:196,24:492])>10 and np.std(settings[320:436,24:332])>10, 'Settings lack theme or save folder controls'
    assert not np.array_equal(settings[88:500,24:700],settings_en[88:500,24:700]), 'RU/EN settings do not change'
    assert not np.array_equal(image[100:522,24:976],main_en[100:522,24:976]), 'RU/EN main UI does not change'
    dropdown=np.array(Image.open(root/f'theme-{name}-dropdown.png').convert('RGB'))
    assert np.max(abs(dropdown[168, 630].astype(int)-np.array(expected)))<=1, 'Dropdown uses the system menu background'
    assert not np.array_equal(image[160:274,336:636],dropdown[160:274,336:636]), 'Dropdown was not rendered'
    assert np.unique(dropdown[166:268,372:600].reshape(-1,3),axis=0).shape[0]>30, 'Dropdown text or selection missing'
    print(f"{name}: palette, non-empty controls and text checked")
assert np.mean(abs(images[0].astype(float) - images[1].astype(float))) > 150
icon=Image.open(Path('assets/Screencast.ico'))
required={16,20,24,28,30,32,36,40,44,48,56,60,64,72,80,96,112,128,144,160,192,256}
assert {(n,n) for n in required} <= icon.ico.sizes(), 'Missing native icon size; Windows would have to resample another entry'
assert Path('assets/Screencast-shell-0.2.6.ico').read_bytes()==Path('assets/Screencast.ico').read_bytes(), 'Shortcuts and executable use different icon artwork'
for size in icon.ico.sizes():
    alpha=np.asarray(icon.ico.getimage(size).convert('RGBA'))[:,:,3]
    y,x=np.where(alpha>128)
    assert x.max()-x.min()+1>=int(size[0]*.9) and y.max()-y.min()+1>=int(size[1]*.9), 'Icon artwork leaves excessive transparent padding'
    # The front plate and camera must keep a distinct edge, even in small shell icons.
    if size[0] in (32,48,64):
        pixels=np.asarray(icon.ico.getimage(size).convert('RGBA')).astype(int)
        row=pixels[int(size[0]*.51),int(size[0]*.18):int(size[0]*.38),0]
        assert np.max(abs(np.diff(row)))>100, 'Camera edge has been blurred across too many pixels'
print('All focused controls match unfocused rendering; icon artwork fills every ICO size')
