# Screencast 0.4.2

- Единый установщик CONNECT Installer на Go/Wails; общий исходник хранится в `sanderstripa/connect/installer-core`.
- Кнопка «Принять и установить» находится на том же уровне, что в VPN, в обеих темах и на обоих языках.
- Сохранены установочные адаптеры, настройки и особенности продукта из проверенных пользователем локальных сборок.
- Изменения интерфейса и функциональности самого приложения не вносились, кроме номера версии.

Пользователь подтвердил установку всех пяти предыдущих локальных сборок. Автоматические проверки текущей сборки указаны в GitHub Actions; повторная ручная установка каждого финального SHA не заявляется.

Screencast 0.4.1 for Windows 11 x64.

- Match installation settings and completion positions to the pinned CONNECT VPN installer: lower Accept and install, align the final application title and Done button.
- Render the actual installer using Edge WebView2, the same engine as CONNECT. Reuse the unchanged CONNECT CSS and identical Montserrat font, with real HTML input/button components throughout.
- Verify exact computed typography, borders, opacity, dimensions and vertical coordinates in Russian/English and light/dark themes. No CSS scaling, zoom, filters or compositor transforms are applied to these controls.
- Preserve installation/extraction, folder selection, shortcuts, theme/language transfer and uninstall behavior.
