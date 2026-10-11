# Verification status — desktop overlay and native installer

Verified executable source: 6c00120, 2026-10-11.
[Windows CI run 38112019435](https://github.com/sanderstripa/screencast/actions/runs/38112019435): SUCCESS.

Automated checks passed:
- Independent system/microphone gain and mute; packet alignment, silence and saturation.
- Spectral attenuation of noise; intensity changes output; 0% is exact bypass; a speech-level tone is retained.
- Smooth motion convergence and decoded H.264 circle present from frame 0 with interpolated movement.
- H.264/AAC, timing, stereo test tones and resized-window letterboxing.
- Windows ML RVM recurrent inference and background composition.
- Both production app themes fit a single 1024×768 screen; snapshots visually inspected.
- Native installer payload extraction and removal in an isolated directory.
- All four installer stages in Russian/English and light/dark themes: sixteen snapshots checked.

The production desktop overlay passed a local synthetic-camera test: a screen snapshot using BitBlt/CAPTUREBLT and Windows Graphics Capture both contained the circle. Only a synthetic center crop was saved to MP4; no personal desktop or camera images were published.

Physical-camera and encoder checks were run locally. Detailed hardware diagnostics remain local. Encoder microbenchmarks exclude screen readback and audio capture and do not establish a complete recording frame rate.

Long recordings, subjective speech/noise quality, person/hair segmentation, 4K/HDR, device removal and all monitor/process-audio combinations still require acceptance in tests/MANUAL.md. They are not claimed as passed.

The application download button was tested locally with the model absent: model and license downloaded, SHA256 matched, and blur mode became selected. The final installer also passed isolated extraction/removal locally (exit 0).
