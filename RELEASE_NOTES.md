Screencast 0.3.9 for Windows 11 x64.

- Fix the installation-folder picker freezing or closing the installer by initializing its Windows UI thread in the Shell-compatible STA apartment.
- Handle folder-dialog cancellation separately from errors and accept filesystem folders only.
- Verify real folder selection, cancellation and reopening, preserving the destination and installation settings.
- Keep the approved installer visuals, theme/language transfer and application recording behavior unchanged.
