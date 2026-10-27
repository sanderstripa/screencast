# Approved graphite/glass icon family

Version 0.4.7: the first-column concept approved on 27 October 2026.
Dark continuous-corner square; restrained glass glyph; no ribbon. Transparent exterior corners.

Native SVG is the editable source. `scripts/render-app-icons.cjs` deterministically renders each physical size from vector geometry; install the development-only `sharp` package and run `node scripts/render-app-icons.cjs`. Do not run historical ribbon generators.

Small frames use a minimum stroke weight to keep the symbol readable. ICO frames retain full alpha. Installers consume the same product ICO and PNG, and existing versioned shell aliases retain the updated artwork.
