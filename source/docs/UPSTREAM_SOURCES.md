# Проверенные внешние источники Atlas Launcher

## Java Runtime

Локальная Java Runtime загружается только по HTTPS из Eclipse Adoptium API. Официальный API публикует маршруты для выбора актуального бинарного архива по версии Java, ОС, архитектуре, типу образа и JVM; также он предоставляет маршрут checksum для выбранного релиза.[1]

Для Windows x64 используется ZIP-образ JRE/HotSpot. Архив сохраняется через менеджер загрузок с проверкой контрольной суммы, затем распаковывается в папку данных Atlas Launcher (`runtime/<major>`). Он не запускает MSI, не меняет `JAVA_HOME`, PATH и реестр Windows.

Доступные LTS-ветки, публикуемые API на момент проверки: Java 8, 11, 17, 21 и 25.[2] Лаунчер выбирает требуемую мажорную ветку по `javaVersion.majorVersion` в метаданных выбранной версии Minecraft: Java 8 как безопасный базовый runtime для старых версий, Java 17 для современных версий и Java 21 при явном требовании metadata.

## Minecraft Java Edition

Для игры будет использоваться официальный version manifest Mojang/Piston Meta. Он публикует список релизов и snapshots, а URL каждой версии ведёт к детальному JSON с метаданными client JAR, библиотек, assets и их SHA-1. Лаунчер не будет использовать непроверенные зеркала или готовые игровые архивы.

## Безопасность

Все URL загрузок допускаются только по HTTPS. Целевой файл записывается через временный файл и заменяет финальный путь только после успешной проверки хеша. Встраивание секретов API в приложение запрещено: ключ CurseForge остаётся сеансовым, а идентификатор приложения Microsoft вводится владельцем при реализации авторизации.

## References

[1]: https://api.adoptium.net/q/swagger-ui/ "Eclipse Adoptium API v3 — OpenAPI UI"
[2]: https://api.adoptium.net/v3/info/available_releases "Eclipse Adoptium API — Available releases"

## Установка Minecraft и загрузчиков

Mojang публикует актуальный манифест по адресу `https://launchermeta.mojang.com/mc/game/version_manifest_v2.json`. Каждая запись содержит идентификатор, тип, URL JSON конкретной версии и SHA-1. Детальные JSON содержат client JAR, asset index, библиотеки и Windows-правила.

Fabric Meta: `https://meta.fabricmc.net/v2/versions/loader/{minecraftVersion}`. Ответ включает выбранный loader, intermediary и `launcherMeta` с библиотеками, хешами и main class клиента.

Quilt Meta: `https://meta.quiltmc.org/v3/versions/loader/{minecraftVersion}`. Ответ включает loader, intermediary/hashed и `launcherMeta` с библиотеками и main class клиента.

Atlas скачивает файлы только через очередь с HTTPS и опубликованной проверкой хеша. Сторонние установщики не запускаются автоматически: пользователь явно выбирает тип загрузчика и версию.

[3]: https://launchermeta.mojang.com/mc/game/version_manifest_v2.json "Mojang Minecraft version manifest"
[4]: https://meta.fabricmc.net/ "Fabric Meta"
[5]: https://meta.quiltmc.org/ "Quilt Meta"

### Проверенные профили Fabric и Quilt

Fabric metadata API публикует перечень версий загрузчика по адресу `https://meta.fabricmc.net/v2/versions/loader`; готовый профиль выбранной пары доступен по `https://meta.fabricmc.net/v2/versions/loader/{minecraft}/{loader}/profile/json`. В ответе присутствуют `id`, `inheritsFrom`, `mainClass`, аргументы и Maven-библиотеки, а в большинстве записей также контрольные суммы.

Quilt публикует список версий по `https://meta.quiltmc.org/v3/versions/loader`, а профиль пары Minecraft/loader — по `https://meta.quiltmc.org/v3/versions/loader/{minecraft}/{loader}/profile/json`. Он содержит `id`, `inheritsFrom`, `mainClass` и перечень Maven-библиотек. Официальная страница Fabric прямо рекомендует metadata API для разработки лаунчеров.

[6]: https://meta.fabricmc.net/v2/versions/loader/1.20.1/0.15.11/profile/json "Пример Fabric loader profile"
[7]: https://meta.quiltmc.org/v3/versions/loader/1.20.1/0.26.4/profile/json "Пример Quilt loader profile"
[8]: https://fabricmc.net/use/installer/ "Fabric installer и рекомендации для разработчиков лаунчеров"

> До полной реализации объединения версионных профилей и аргументов запуска интерфейс показывает рабочую установку Vanilla. Fabric, Quilt, Forge и NeoForge не будут рекламироваться как установленные, пока для каждого не появится полный рабочий путь установки и запуска.


## Microsoft device-code flow (лицензионный вход)

Источник: https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-device-code

Для входа без ввода пароля в лаунчере используется `POST https://login.microsoftonline.com/{tenant}/oauth2/v2.0/devicecode` с зарегистрированным `client_id` и разрешениями. Ответ содержит `device_code`, `user_code`, `verification_uri`, `expires_in` и `interval`. Затем клиент опрашивает `POST /{tenant}/oauth2/v2.0/token` с `grant_type=urn:ietf:params:oauth:grant-type:device_code`, тем же `client_id` и `device_code`. Периодические ответы `authorization_pending` не являются ошибкой; интервал опроса должен соблюдаться. Для доступа без повторного входа требуется scope `offline_access`; refresh token следует хранить только локально и защищённо. Atlas не собирает пароль и не реализует обход лицензии.

> Для production-входа требуется собственный зарегистрированный Microsoft application (client ID) владельца Atlas; интерфейс должен запрашивать его настройку, а не использовать чужой идентификатор.

[9]: https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-device-code "Microsoft identity platform — OAuth 2.0 device authorization grant flow"

## Проверенные источники установщиков загрузчиков

- Fabric Meta прямо поддерживает интеграцию сторонних лаунчеров. Для установки выбирается версия через `GET /v2/versions/loader/{minecraftVersion}`, затем запрашивается профиль `GET /v2/versions/loader/{minecraftVersion}/{loaderVersion}/profile/json`. Профиль содержит идентификатор, `inheritsFrom`, `mainClass`, аргументы и Maven-библиотеки.
- Quilt Meta v3 публикует равнозначные endpoints `/v3/versions/loader/{gameVersion}` и `/v3/versions/loader/{gameVersion}/{loaderVersion}/profile/json`. Официальная документация требует содержательный User-Agent.
- NeoForge публикует installer JAR на официальном Maven по шаблону `https://maven.neoforged.net/releases/net/neoforged/neoforge/{version}/neoforge-{version}-installer.jar`. Atlas будет запускать такой installer только через уже установленную локальную Java и только в staging-папке выбранного экземпляра, после чего проверит ожидаемый launcher profile.
- Forge использует официальный Maven: `https://maven.minecraftforge.net/net/minecraftforge/forge/{minecraft-forge-version}/forge-{minecraft-forge-version}-installer.jar`. Atlas получает соседний файл `.sha1` с того же HTTPS-источника, проверяет JAR через очередь перед запуском локальной Java с `--installClient` и подтверждает созданный launcher profile в `game/versions`.

[10]: https://github.com/FabricMC/fabric-meta "Fabric Meta API — README and launcher profile endpoints"
[11]: https://meta.quiltmc.org/ "Quilt Meta API v3"
[12]: https://docs.neoforged.net/user/docs/server/ "NeoForge documentation — official installer Maven pattern"

### Проверка официальных Maven-источников

Проверен индекс `https://maven.neoforged.net/releases/net/neoforged/neoforge/`: он публикует версии и checksum-файлы рядом с артефактами. Для NeoForge допустим только официальный HTTPS installer по шаблону `.../{version}/neoforge-{version}-installer.jar`; перед запуском Atlas должен получить checksum из того же официального репозитория и использовать исключительно локальную Java в staging-папке экземпляра.

Forge использует документированный Maven-адрес `https://maven.minecraftforge.net/net/minecraftforge/forge/{minecraft-forge-version}/forge-{minecraft-forge-version}-installer.jar` и соседний SHA-1. Atlas принимает только точный номер в формате Maven (например, `1.20.1-47.4.22`), получает checksum с того же официального HTTPS-источника и не запускает переданные сторонними проектами URL.

[13]: https://maven.neoforged.net/releases/net/neoforged/neoforge/ "NeoForge official Maven index"
[14]: https://maven.minecraftforge.net/net/minecraftforge/forge/ "Minecraft Forge official Maven index"

Проверка доступности от 2026-08-11: `https://maven.neoforged.net/releases/net/neoforged/neoforge/21.1.142/neoforge-21.1.142-installer.jar` вернул `200`, `Content-Type: application/java-archive`, а соседний `https://maven.neoforged.net/releases/net/neoforged/neoforge/21.1.142/neoforge-21.1.142-installer.jar.sha256` вернул SHA-256. Это подтверждает используемую реализацией схему «получить checksum с того же официального Maven → скачать через DownloadManager с SHA-256 → запустить только локальной Java Atlas». Значение версии приведено исключительно как проверенный пример; Atlas требует, чтобы пользователь указал точную версию NeoForge.


## Qt TLS / OpenSSL runtime (проверено 2026-08-11)

- Qt for Windows requirements: <https://doc.qt.io/qt-5/windows-requirements.html>. Qt 5.15 Network использует динамически доступный OpenSSL для HTTPS в данной MinGW-поставке.
- Официальный исходный архив OpenSSL 1.1.1w: <https://github.com/openssl/openssl/releases/download/OpenSSL_1_1_1w/openssl-1.1.1w.tar.gz>.
- Опубликованная OpenSSL SHA-256: <https://github.com/openssl/openssl/releases/download/OpenSSL_1_1_1w/openssl-1.1.1w.tar.gz.sha256>; значение `cf3098950cb4d853ad95c0841f1f9c6d3dc102dccfcacd521d93925208b76ac8`.
- Воспроизводимая сборка Atlas: `scripts/build_openssl_win64.sh` собирает только `libcrypto-1_1-x64.dll` и `libssl-1_1-x64.dll` из исходников после SHA-256-проверки; `scripts/deploy_windows.ps1` кладёт их рядом с EXE.
- Проверка в изолированной Wine Windows-среде с тем же Qt 5.15 runtime: `QSslSocket::supportsSsl()` вернул `true`, runtime определён как `OpenSSL 1.1.1w 11 Sep 2023`, а запрос `https://piston-meta.mojang.com/mc/game/version_manifest_v2.json` завершился HTTP 200 и получил 273470 байт. Это устраняет ранее воспроизведённый отказ `TLS initialization failed` на уровне Qt/OpenSSL runtime; запуск на чистой физической Windows 7 SP1 x64 остаётся обязательной финальной проверкой совместимости.
