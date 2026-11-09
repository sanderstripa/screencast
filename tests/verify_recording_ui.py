from pathlib import Path
import sys
import av
folder=Path(sys.argv[1])
files=list(folder.glob('Screencast_*.mp4'))
assert len(files)==2,files
for path in files:
    video=av.open(str(path))
    frames=list(video.decode(video=0))
    assert len(frames)>30,'Recording did not continue behind the settings sheet'
    assert all(f.width>0 and f.height>0 for f in frames)
    assert frames[-1].time>1,'Recording duration is too short'
    print(f'{path.name}: {len(frames)} decoded frames, {frames[-1].time:.2f}s')
