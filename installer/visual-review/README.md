# Shared installer HTML and standalone preview

The current HTML/CSS/script are shared with the production 0.4.1 WebView2 installer.
When opened as a normal browser page they retain the standalone simulated
installation preview described below. In the real installer, WebView2 messages
connect the same controls to the native backend. Historical 0.3.7 approval images
and original ribbon artwork remain archived here; current source provenance and
exact CONNECT comparison checks are described in `../README.md`.

## Original 0.3.7 review record

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

**Approval received:** the user approved these screens and explicitly requested
publication. The archived screenshots preserve the original review. The production
native integration and its verification are described in `../README.md`.

Screens: `?stage=welcome`, `settings`, `installing`, `finish`.
Additional query parameters: `theme=light|dark`, `lang=ru|en`.

`screens/review-light.png` and `screens/review-dark.png` show all four stages.
Sixteen individual screenshots cover RU/EN and both themes. Browser checks passed
font loading, clipping, version/footer content, absence of extension controls,
navigation and persistent preview switch states (`screens/verification.txt`).
At the review stage, the source installer, extraction/uninstall code, application
version, Windows artifacts and main branch were not changed or built. Subsequent
release work is performed only after the explicit approval above.
