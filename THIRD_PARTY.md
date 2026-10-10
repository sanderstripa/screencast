# Third-party components

- Montserrat: https://github.com/google/fonts/tree/main/ofl/montserrat — SIL Open Font License 1.1; license in `assets/Montserrat-OFL.txt`. Privately loaded for this application only; not installed into Windows.
- Windows SDK, Media Foundation, Windows Graphics Capture, WASAPI and Windows ML are platform components. No external runtime is bundled with the core application.
- Optional human foreground model: [Robust Video Matting](https://github.com/PeterL1n/RobustVideoMatting), MobileNetV3 FP32 ONNX v1.0.0, GPL-3.0. It is downloaded separately and is NOT included in the portable core package. Keep its license with the model; review GPL obligations before redistributing an app/model bundle. The repository owner's application licensing decision remains open.
- PyAV and NumPy are used only by CI to independently decode and inspect the synthetic test recordings; they are not application dependencies.

Implementation references:
- https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/screen-capture
- https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/
- https://learn.microsoft.com/en-us/windows/win32/medfound/using-the-sink-writer
- https://github.com/PeterL1n/RobustVideoMatting/blob/master/documentation/inference.md
