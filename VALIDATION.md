# Verification status — camera and live-controls update

Verified source: 3241846, 2026-10-11.
[Windows CI run 38110236106](https://github.com/sanderstripa/screencast/actions/runs/38110236106): SUCCESS.

Automated checks passed:
- Independent system/microphone gain and mute; packet alignment, silence and saturation.
- Spectral attenuation of broadband noise; intensity changes the output; 0% is exact bypass; a speech-level tone is retained.
- Smooth motion convergence without fixed-target overshoot.
- Decoded H.264 circle present on every frame including frame 0, with interpolated movement to the opposite corner.
- H.264/AAC, timing, stereo test tones and resized-window letterboxing.
- Windows ML RVM recurrent inference and background composition.
- Both production UI themes: palette, controls and text; snapshots visually inspected.

Physical-camera and encoder checks were also run locally. Detailed hardware diagnostics remain local.
Encoder microbenchmarks exclude screen readback and audio capture; they do not establish the frame rate of a complete desktop recording or a percentage improvement over the previous version.

Portable core: 1,398,393 bytes. Includes fonts and three interior images; the background model is installed separately with scripts/setup-background.ps1.

Long recordings, subjective real speech/noise quality, person/hair segmentation, 4K/HDR, device removal and all monitor/process-audio combinations still require acceptance in tests/MANUAL.md. They are not claimed as passed.