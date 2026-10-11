Screencast 0.2.5 for Windows 11 x64.

- Repeat launches through desktop/Start shortcuts restore the existing window instead of opening another instance. The guard also covers concurrent launches and startup/shutdown.
- App icon requests now use the same full-size icon as the installer, including the ICON_SMALL request that still returned 16x16 in the previous release.
- Desktop, Start menu and uninstall shortcuts share a versioned, multi-resolution icon asset. Small/medium ICO entries use native 32-bit DIB images; the 256-pixel image remains lossless PNG. Updating shortcuts also refreshes their shell icon references.
- Original icon artwork, composition and relative proportions are unchanged.

Download Screencast-Setup-x64.exe for installation or Screencast-Windows-x64.zip for portable use. Background processing uses a separately downloaded local model (about 15 MB).
