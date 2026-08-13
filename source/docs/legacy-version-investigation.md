# Диагностика legacy-версий Minecraft

## Официальный источник

- Манифест Mojang: https://launchermeta.mojang.com/mc/game/version_manifest_v2.json

Текущий установщик Atlas использует этот манифест (`MinecraftInstallService::requestManifest`) и в интерфейс передаёт только записи с `type == "release"`. Поэтому Alpha, Beta и старые экспериментальные типы не появляются, даже если официально присутствуют в manifest.

## Первичное техническое наблюдение

Код установки ожидает современную структуру metadata: `downloads.client`, `assetIndex`, `libraries[].downloads` и `libraries[].downloads.classifiers`. Для многих legacy-версий требуется разбор наследуемых версионных JSON (`inheritsFrom`) и/или legacy asset index. Без этого Atlas может показать версию, но не скачать корректный набор файлов или не собрать корректную команду запуска.

## Проверка официальных метаданных — 13 августа 2026

Скрипт `tools/inspect_legacy_metadata.py` повторно получил актуальные JSON из `https://launchermeta.mojang.com/mc/game/version_manifest_v2.json` для `1.7.10`, `1.7.4`, `b1.7.3` и `a1.2.6`.

| Версия | Тип из manifest | Asset index | Client JAR | Библиотеки |
|---|---|---|---|---:|
| 1.7.10 | `release` | `1.7.10` | есть | 33 |
| 1.7.4 | `release` | `1.7.4` | есть | 27 |
| b1.7.3 | `old_beta` | `pre-1.6` | есть | 13 |
| a1.2.6 | `old_alpha` | `pre-1.6` | есть | 13 |

Актуальные JSON для 1.7.10 и 1.7.4 уже содержат современный блок `downloads.artifact` с SHA-1 для обычных библиотек и `downloads.classifiers` для Windows natives. Поэтому ошибка 1.7.x не должна устраняться предположением, что у всех этих версий отсутствует `downloads`: основная проверяемая ветка должна сохраниться, а резервная Maven-ветка нужна только для действительно старых или сторонних metadata без `downloads.artifact`.

`assetIndex` у 1.7.10 и 1.7.4 использует защищённый `https://launchermeta.mojang.com/...`; у Beta/Alpha ID равен `pre-1.6`. Установщик должен принимать любой HTTPS asset-index URL из официальных метаданных, а категории `old_beta` и `old_alpha` следует явно запрашивать и отображать в интерфейсе.

Главная несовместимость для запуска legacy JSON находится в `LaunchService::classpathFor()`: он берёт путь только из `downloads.artifact`. Если библиотека была скачана резервной Maven-веткой, но не имеет этого блока, она не попадёт в classpath. Исправление должно использовать единый путь из `downloads.artifact.path` либо вычислять путь по Maven coordinate `name`.

### Проверенный путь assets

В [профиле Forge 1.7.10](https://raw.githubusercontent.com/MinecraftForge/FML/master/jsons/1.7.10.json) указано `--assetsDir ${assets_root}` и `--assetIndex ${assets_index_name}`. Официальный JSON Mojang 1.7.10 использует те же аргументы. Следовательно, Atlas должен передавать корневую папку `game/assets`, а не создавать неподтверждённую копию `assets/virtual`; текущая подстановка `${assets_root}` уже корректна. Для этой версии также требуется файл logging-конфигурации `logging.client.file` и подстановка `${path}` в JVM-аргументах.
