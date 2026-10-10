# Verification status

First working build; full hardware acceptance remains pending.

- Local development environment: Windows, no MSVC or Windows SDK discovered. Local compilation has not been claimed.
- Verified source commit: `f7173ba` (2026-10-11, user timezone UTC+5).
- [GitHub Actions run 38087415331](https://github.com/sanderstripa/screencast/actions/runs/38087415331): SUCCESS on Windows Server 2022, MSVC and Windows SDK 10.0.26100.0.
- Automated audio tests: passed timestamp alignment, missing-packet silence, independent channels, stale-ring protection, peak clipping and attenuation of weak noise.
- MP4 tests: two real Media Foundation recordings successfully decoded with PyAV. Each contains 90 H.264 frames over 3 seconds; silent recording has no audio stream, other recording has stereo AAC at 48 kHz with verified 440/660 Hz tones. Frame timing and window-resize letterboxing passed.
- Optional background model: checksum verified; loaded through Windows ML, two inference/composition passes succeeded with recurrent state reuse. This verifies integration, not person segmentation quality on a real webcam.
- UI snapshots: both production rendering routines exercised in a hidden window; palette, presence of text/controls, and distinct light/dark images checked automatically. Both images were visually inspected. On the CI runner's 1024×768 desktop the UI uses scrolling.
- Portable archive: 529,583 bytes, executable: 450,560 bytes. Core archive includes Montserrat and no optional model. Model download: 14,975,696 bytes.
- Local Windows build 26100: the earlier `f133a46` binary also successfully ran encoder and Windows ML model smoke tests. It was not locally compiled; binary came from the successful CI build.
- Physical screen/window capture, process audio isolation, microphones, cameras, model inference, long recordings and device failure handling: require execution on real Windows 11 hardware; see `tests/MANUAL.md`.

This application is a first implementation, not yet a verified production replacement for OBS.
