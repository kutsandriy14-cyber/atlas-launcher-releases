# Legacy Fabric: техническая справка для Atlas Launcher

Дата проверки: 2026-08-12.

## Официальные источники

| Источник | Подтверждённый факт | URL |
|---|---|---|
| Legacy Fabric Downloads | Legacy Fabric предлагает поддержку Minecraft 1.12.2 и публикует отдельный Windows installer. | https://legacyfabric.net/downloads.html |
| Legacy Meta README | API совместим с форматом Fabric Meta: список доступен через `/v2/versions/loader/:game_version`, а launcher profile — через `/v2/versions/loader/:game_version/:loader_version/profile/json`. | https://github.com/Legacy-Fabric/legacy-meta |
| Legacy Meta API | Реальный список Legacy Fabric для 1.12.2 доступен по `https://meta.legacyfabric.net/v2/versions/loader/1.12.2`; профиль версии 0.19.3 доступен по `https://meta.legacyfabric.net/v2/versions/loader/1.12.2/0.19.3/profile/json`. | https://meta.legacyfabric.net/v2/versions/loader/1.12.2 |

## Результаты проверки

Обычный Fabric Meta по адресу `https://meta.fabricmc.net/v2/versions/loader/1.12.2` вернул HTTP 400 с JSON `[]`. Это ожидаемое отсутствие современного Fabric для 1.12.2, а не ошибка TLS или User-Agent Atlas.

Legacy Meta для той же версии вернул реальный список: текущая первая стабильная версия — `0.19.3`. Профиль возвращает поля `id` (`fabric-loader-0.19.3-1.12.2`), `inheritsFrom` (`1.12.2`), `mainClass` и `libraries`, поэтому его можно установить существующим механизмом launcher-profile Atlas после добавления отдельного типа `LegacyFabric` и базы API `https://meta.legacyfabric.net/v2`.

## План изменения

Нужно добавить отдельный вариант загрузчика `Legacy Fabric` в доменную модель и мастер сборок. Он должен использовать официальный Legacy Meta, поддерживать выбор точной версии loader и обрабатываться аналогично Fabric при скачивании launcher profile и библиотек. Обычный Fabric остаётся отдельным вариантом для современных версий.

## Проверка профиля 0.19.3 для Minecraft 1.12.2

Официальный профиль `https://meta.legacyfabric.net/v2/versions/loader/1.12.2/0.19.3/profile/json` содержит библиотеку `org.lwjgl.lwjgl:lwjgl-platform:2.9.4+legacyfabric.17` с `natives.windows = "natives-windows"` и правилом извлечения с исключением `META-INF/`. Это **не** обычный Maven JAR для classpath: в официальном каталоге `https://maven.legacyfabric.net/org/lwjgl/lwjgl/lwjgl-platform/2.9.4+legacyfabric.17/` опубликованы classifier-артефакты `natives-linux`, `natives-osx` и `natives-windows` (а также POM), но базового `lwjgl-platform-2.9.4+legacyfabric.17.jar` нет.

При разборе launcher profile Atlas должен выбрать `natives.windows`, скачать `lwjgl-platform-2.9.4+legacyfabric.17-natives-windows.jar`, извлечь его в natives-папку экземпляра с исключением `META-INF/` и не добавлять отсутствующий базовый артефакт в classpath. Это объясняет подтверждённый ответ HTTP 404 во время реального Wine-теста.
