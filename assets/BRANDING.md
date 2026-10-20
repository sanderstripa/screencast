# Screencast vector camera icon

The approved lavender twisting ribbon, inner white camera square and red recording dot are redrawn as native Bezier paths and gradients in `Screencast.svg`. There are no embedded raster images, blur filters, grain, external tile or shadow.

`scripts/render-branding.cjs` renders each of 33 physical Windows icon sizes directly from the vector through Chromium at its final pixel size. Camera edges and the red dot align to the physical pixel grid at small sizes. No bitmap is resized to produce another size. ICO DIB entries store the native pixels unchanged; the 256 entry is PNG. The 2048 and 4096 exports also render directly from the vector, rather than enlarging a raster master.

`icon-sizes/` contains the exact individual frames. All historical shell aliases contain the same artwork. Installer icon-location overrides and Windows cache notifications are retained. The real installation/upgrade checks verify old aliases and actual Desktop/Start/uninstall shortcuts.

Generate assets with `node scripts/render-branding.cjs` (Playwright and pngjs; installed Chrome). The app and installer consume the identical ICO; the Wails installer window uses the matching native 256 PNG.
