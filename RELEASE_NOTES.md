# Screencast 0.5.6

- Cache the blurred background upload and rasterize the complete moving sheet into one transparent Direct2D layer per transition.
- Animation frames draw two cached bitmaps instead of allocating/uploading a full-window DIB and redrawing text and 24 shadow layers.
- Remove hidden control movement, visibility updates and synchronous whole-window child repaint from animation ticks.
- Keep 220ms cubic easing and invalidate caches on theme, window size and DPI changes.
