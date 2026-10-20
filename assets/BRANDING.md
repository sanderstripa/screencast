# Screencast lavender camera icon

`Screencast-master.png` is the native 1254 x 1254 transparent image redrawn with the built-in imagegen tool from the user-supplied concept on 2026-10-20. The outer white tile is removed; the inner white camera square remains.

`render-icon.ps1` packages the tightly framed mark without changing its aspect ratio. The 2048 PNG is a large export, not a claim of additional source detail. ICO files contain 22 physical sizes (16 through 256); small entries are 32-bit DIB and the 256 entry is PNG. The legacy shell filename is an identical compatibility alias; new shortcuts use the 0.4.5 filename to avoid reusing the old icon location.

Generation prompt: Redraw the reference as a high-resolution Windows application icon. Preserve the lavender metallic twisting aperture ribbon, orientation, dimensional shading and inner rounded ivory square with a lavender video camera and one red recording dot. Remove the entire outer white tile, its shadow and page background. Transparent alpha outside the ribbon. Enlarge the complete mark with tiny safe margins; no cropping, new elements or text. Keep the symbol clear at small sizes.

The pinned CONNECT installer receives the same ICO and a 256 PNG window icon at build time. Only the Screencast icon-location strings in its product adapter are updated. Installer logic and UI assets remain unchanged.

All historical shell aliases (0.2.5, 0.2.6, 0.4.3 and 0.4.4) contain the same current artwork. Installer branding overrides refresh the Windows shell cache after creating shortcuts; an isolated upgrade test corrupts an old alias and verifies it is replaced on reinstallation.
