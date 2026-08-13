# TLS audit — 2026-08-11

На Windows-скриншоте Atlas показывает `TLS initialization failed` при запросе списка Mojang.

Проверка portable-пакета показала:

- `dist/AtlasLauncher-win64` не содержит `libssl*.dll`, `libcrypto*.dll`, `qopensslbackend.dll`, `qschannel.dll` или каталога `tls`.
- `Qt5Network.dll` содержит строки `OPENSSL_init_ssl`, `TLS_method` и `QSslSocket: OpenSSL >= 1.1.1 is required`, то есть Qt Network ожидает динамический OpenSSL 1.1.1+.
- Qt-комплект содержит `Qt5Network.dll`, но в поставке нет OpenSSL runtime и TLS backend plugin.
- `scripts/deploy_windows.ps1` запускает `windeployqt --release --compiler-runtime --no-translations`, но не проверяет и не копирует OpenSSL DLL/TLS backend.

Следствие: Qt Network загружается, но HTTPS не может начать TLS handshake. Пакет нельзя считать рабочим до добавления совместимого OpenSSL runtime/backend и проверки запроса к Mojang на чистой Windows 7 SP1 x64.
