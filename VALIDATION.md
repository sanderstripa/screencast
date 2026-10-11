# Verification status — desktop overlay and native installer

Verified executable source: 01c7712, 2026-10-11.
[Windows CI run 38117717564](https://github.com/sanderstripa/screencast/actions/runs/38117717564): SUCCESS.

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

Version 0.2.1: the visible production window had zero idle parent repaints over two seconds, both in Windows CI and locally on Windows 11. Buffered presentation and child clipping prevent parent/background overdraw. Installer snapshots cover active/inactive shortcut switches, centered completion checkbox, absence of duplicate launch and completion Cancel, and no native caption. Final isolated installer extraction/removal passed locally (exit 0).

Version 0.2.2: decoded synthetic MP4 verifies camera initially off, ordinary orientation, mirrored orientation and camera off again; AAC verifies microphone absent/present/absent while the system tone remains continuous. Device lifecycle tests verify lazy opening, no redundant reopening and release on disable. Physical camera and microphone each passed two open/use/close cycles locally; only numerical results were saved. Theme snapshots with mouse focus flags are pixel-identical to normal snapshots in both themes. This does not claim that every physical-device combination has been tested in a long recording.

Version 0.2.3: every app control was rendered with ODS_FOCUS enabled and matched the unfocused snapshot exactly, in both themes. ICO artwork occupancy passed for every embedded size with pixel rounding accounted for. Bilinear interpolation/edge clamping passed. Windows ML output retains source dimensions; the production matte compositor preserved an opaque one-pixel 1280x720 detail pattern exactly. Bounded parallel row composition was added after a local performance regression was found. In the final local physical-camera run, effects matched the observed input cadence across all tested modes; numeric diagnostics remain local. This is a camera-pipeline check and does not establish full desktop-recording FPS for every device or lighting condition.
