"""Decode actual Media Foundation output; do not just inspect file existence."""
from pathlib import Path
import sys
import av
import numpy as np

root = Path(sys.argv[1])
for name, has_audio in (("synthetic-silent.mp4", False), ("synthetic-audio.mp4", True)):
    path = root / name
    with av.open(str(path)) as container:
        video = container.streams.video[0]
        assert video.codec_context.name == "h264"
        frames = list(container.decode(video))
        assert len(frames) == 90, (name, len(frames))
        assert all(f.width == 640 and f.height == 360 for f in frames)
        times = [float(f.pts * f.time_base) for f in frames]
        assert all(b > a for a, b in zip(times, times[1:]))
        assert abs(times[-1] - 89 / 30) < 0.002
        assert bool(container.streams.audio) == has_audio
        # Source changes size halfway through; borders must become black, not stretched.
        before = frames[15].to_ndarray(format="rgb24")
        resized = frames[45].to_ndarray(format="rgb24")
        assert before[:, :8].mean() > 20
        assert resized[:, :8].mean() < 6
    if has_audio:
        with av.open(str(path)) as container:
            audio = container.streams.audio[0]
            assert audio.codec_context.name == "aac"
            chunks = list(container.decode(audio))
            assert chunks and chunks[0].sample_rate == 48000
            assert chunks[0].layout.name == "stereo"
            decoded = np.concatenate([chunk.to_ndarray() for chunk in chunks], axis=1)
            assert abs(decoded.shape[1] / 48000 - 3) < 0.08
            for channel, expected in enumerate((440, 660)):
                signal = decoded[channel, 4800:52800]
                peak = np.argmax(abs(np.fft.rfft(signal * np.hanning(len(signal))))) * 48000 / len(signal)
                assert abs(peak - expected) < 2, (peak, expected)
                assert 0.12 < np.sqrt(np.mean(signal ** 2)) < 0.25
    print(f"{name}: decoded 90 H.264 frames, timing and letterboxing verified; audio={has_audio}")
