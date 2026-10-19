# Screencast 0.4.1 installer

The production installer renders real HTML buttons, inputs, labels and switches
using the installed Microsoft Edge WebView2 Runtime, as CONNECT VPN does. Its
HTML, CSS, script, Montserrat font and approved lavender ribbon are embedded in
the executable and served locally from resources; no remote interface is loaded.
The SDK/loader version is 1.0.2903.40, pinned by SHA-256 in CMake.

`connect-reference/` contains unchanged CONNECT installer sources pinned to
commit `37210bf6dbe1ebcd85938fcb6465bb9b5ed5d184`. The original stylesheet is reused
unchanged. Screencast overrides only the requested vertical coordinates after
removing extension-specific UI: Accept and install at y=387; final title at
y=131.75 and Done at y=312.25 in the 840x540 logical viewport. The launch row
retains the reference vertical level and centers naturally for its product name.
The original font bytes and source-file Git hashes are verified.

`src/installer_web.h` hosts WebView2. `src/installer.cpp` keeps the existing native
extraction, destination validation, shortcut registration, preference transfer,
worker and uninstall implementation. Browser actions are posted to the native
message loop before opening modal Shell UI or closing the window. The folder
picker runs on the STA UI thread. Actual HTML input changes update the existing
native installation destination before installation starts.

The Win32 adapter and generated PNG resources remain geometry/DPI fixtures for
native backend diagnostics. They are not the visible production text renderer.
Production and diagnostic screenshots come from the live WebView2 surface.

`--wizard-test` exercises real HTML settings/theme/language/back controls, text
input, and the actual HTML Browse action through folder select/cancel/reopen.
It also loads the pinned CONNECT sources in the same WebView2 instance and
compares computed CSS/geometry and captured component pixels in RU/EN and both
themes. Path label/input, Browse, primary buttons and on/off toggles must match
with zero pixel tolerance. Only the product name in the centered reference launch
label is substituted so its text width matches Screencast. No extension controls
or guidance are shown in the production application.

`tests/verify_installer.py` validates these screenshots, source/font/resource
hashes, colors, content, version and switch changes. The old headless PNG text is
not used as a typography oracle for the Windows WebView compositor.
`tests/verify_install_flow.ps1` is restricted to the isolated CI runner and tests
actual installation/reinstallation, all four appearance/language combinations,
installed-application startup, unrelated preferences and uninstall/self-removal.

For source-level CSS/geometry auditing run `node installer/verify-connect-parity.cjs`.
For DPI geometry fixtures run `node installer/render-native-assets.cjs` with Node,
Playwright, pngjs and Chrome. `CHROME_PATH` may override the Chrome executable.
The originally approved 0.3.7 review screenshots and ribbon source remain archived
under `visual-review/`; its HTML/CSS are now the shared 0.4.1 presentation source.
