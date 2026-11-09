# Screencast 0.5.4

- Fix unselected context menu starting at row -1, which read outside its item list and produced a blank checked row or a crash.
- Keep no-selection state separate from the keyboard highlight and clamp list viewport indices.
- Exercise the actual right-click context menu and Enter selection in Windows UI tests and while recording, instead of directly opening settings.
