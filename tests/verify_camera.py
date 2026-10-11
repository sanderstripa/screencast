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
