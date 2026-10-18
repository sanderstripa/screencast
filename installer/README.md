# Screencast installer presentation

The user approved `visual-review/` and authorized publication of version 0.3.7.
The release uses that approved Montserrat layout, palette and lavender ribbon.

`render-native-assets.cjs` renders the approved HTML into embedded PNG coverage
at 96/120/144/168/192/240/288/384 DPI, plus separate unchecked-switch regions.
`generated/layout.h` maps real native controls onto the approved DOM positions.
`src/installer_view.h` displays the coverage and wires actual Win32 buttons,
an editable path, the native folder picker, theme/language choices and live worker
progress to the existing installer. It does not require a browser or WebView at
installation time. Chromium is used only to prepare versioned visual resources.

The extraction, destination validation, shortcut creation, installation worker,
uninstall manifest, registration and uninstall implementation remain in
`src/installer.cpp`. Only the registered display version changes to 0.3.7.
Welcome/settings/back navigation follows the approved installer screens.

`tests/verify_installer.py` checks resource/source hashes and actual native wizard
pixels against the approved presentation, excluding the dynamic native path
and live progress bar. `--wizard-test` checks real UI controls without installing.
`tests/verify_install_flow.ps1` runs only on an isolated GitHub Actions runner and
tests actual installation, same-version reinstall, installed-app checks and
actual uninstall including the uninstaller's delayed self-removal.

To regenerate visual resources, use Node with `playwright` and `pngjs`, Chrome,
and run `node installer/render-native-assets.cjs`. `CHROME_PATH` can override
Chrome's location. The approved preview and original image remain archived.
