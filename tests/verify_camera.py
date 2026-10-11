from pathlib import Path
import sys
import av
import numpy as np

frames=list(av.open(str(Path(sys.argv[1])/'synthetic-camera.mp4')).decode(video=0))
assert len(frames)==90
centers=[]
for frame in frames:
    rgb=frame.to_ndarray(format='rgb24')
    mask=(rgb[:,:,0]>180)&(rgb[:,:,1]<70)&(rgb[:,:,2]<70)
    y,x=np.where(mask)
    assert len(x)>5500, 'Webcam circle missing from an encoded frame'
    centers.append((x.mean(),y.mean()))
centers=np.array(centers)
assert np.linalg.norm(centers[0]-[52,52])<3, 'Camera missing or misplaced on first frame'
assert np.linalg.norm(centers[-1]-[587,307])<3
delta=np.diff(centers,axis=0)
assert np.min(delta)>-1, 'Camera motion reverses or jitters'
assert np.max(np.linalg.norm(delta,axis=1))<150, 'Camera jumps between corners'
assert np.count_nonzero(np.linalg.norm(delta,axis=1)>2)>9, 'Movement was not interpolated'
print('Encoded circle present from frame 0; smooth movement reaches the opposite corner')

path=str(Path(sys.argv[1])/'synthetic-live-controls.mp4')
frames=list(av.open(path).decode(video=0))
assert len(frames)==90
for n,frame in enumerate(frames):
    rgb=frame.to_ndarray(format='rgb24')
    if n<15 or n>=75:
        assert np.max(rgb[:100,:100])<65, 'Disabled camera remains in recording'
    else:
        left=rgb[42:62,22:36].mean(axis=(0,1))
        right=rgb[42:62,68:82].mean(axis=(0,1))
        assert (left[0]>180 and right[2]>180) if n<45 else (left[2]>180 and right[0]>180), 'Camera mirror did not switch live'
audio=np.concatenate([f.to_ndarray() for f in av.open(path).decode(audio=0)],axis=1)[0]
for start,microphone in [(.2,False),(1.2,True),(2.2,False)]:
    signal=audio[int(start*48000):int((start+.5)*48000)]
    freq=np.fft.rfftfreq(len(signal),1/48000);spectrum=abs(np.fft.rfft(signal))
    tone=lambda hz:spectrum[abs(freq-hz)<5].max()
    assert tone(440)>100, 'System audio interrupted by microphone toggle'
    assert tone(880)>tone(440)*.6 if microphone else tone(880)<tone(440)*.03, 'Microphone toggle does not change recorded audio'
print('Decoded live camera on/off, ordinary/mirrored orientation and microphone on/off passed')
