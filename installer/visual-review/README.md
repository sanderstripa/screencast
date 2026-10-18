# Screencast 0.3.7 installer — visual review only

This folder is a reviewable presentation prototype, not an executable installer.
Open `index.html`. Its navigation, language/theme selectors and shortcut/launch
switches work locally. Install shows a simulated progress transition only.
Browse selects the path text for editing; native OS folder/minimize/close actions
are deliberately not attached to the prototype. No installation API is called.

The layout and original stylesheet come from `sanderstripa/connect`, commit
`09dab71e7982848d8b44711dc0a9cfc9e39333f2`, `cmd/installer/web/`.
Montserrat and its license are included locally. There is no Chrome extension
option, hint, completion guide or link. The existing Screencast per-user default
installation path is preserved. The footer previews version 0.3.7.

The supplied lavender ribbon is preserved as `assets/ribbon-original.png`.
`assets/ribbon.png` is its transparent cutout for the light/dark UI.
It was prepared with the built-in image_gen editing tool (not the API/CLI).
The exact edit instruction is in `ribbon-edit-prompt.md`. The untouched original
has SHA-256 `b93ff7823f0738f192977be84a2897ece4f42e580df7033a7c0c701a1189eb28`.
The cutout is a visual-review asset; it is not claimed to be pixel-identical to
the source. Approval of its appearance is part of this review.

**Approval gate:** do not finish a Windows installer/app build, tag a version,
publish a release, or change the extraction/shortcut/uninstall implementation
until the user approves these screens. Existing installer/application source and
release packages are intentionally untouched during this visual-review step.

Screens: `?stage=welcome`, `settings`, `installing`, `finish`.
Additional query parameters: `theme=light|dark`, `lang=ru|en`.

`screens/review-light.png` and `screens/review-dark.png` show all four stages.
Sixteen individual screenshots cover RU/EN and both themes. Browser checks passed
font loading, clipping, version/footer content, absence of extension controls,
navigation and persistent preview switch states (`screens/verification.txt`).
The source installer, extraction/uninstall code, application version, existing
Windows artifacts and main branch have not been changed or built.
