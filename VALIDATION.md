# Verification status

Initial implementation; hardware acceptance is pending.

- Local development environment: Windows, no MSVC or Windows SDK discovered. Local compilation has not been claimed.
- GitHub Actions: see the repository's latest `Windows build and MP4 verification` run for the authoritative build/encoding result. This document does not assume the run passed.
- Automated tests: source present; results must be read from the actual CI run.
- Physical screen/window capture, process audio isolation, microphones, cameras, model inference, long recordings and device failure handling: require execution on real Windows 11 hardware; see `tests/MANUAL.md`.

This application is a first implementation, not yet a verified production replacement for OBS.
