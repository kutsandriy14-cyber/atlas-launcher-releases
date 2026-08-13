# Официальные источники версий загрузчиков

## NeoForge

- Главная страница NeoForged: <https://neoforged.net/>
- Проверено 12 августа 2026: официальный сайт показывает раздел **NeoForge installer files** с двумя зависимыми полями: `Minecraft Version` и `NeoForge Version`, а также ссылкой на installer. Это подтверждает требуемую модель интерфейса Atlas: сначала выбирается версия Minecraft, затем только совместимая версия NeoForge.
- На странице поля динамически загружаются, поэтому для автоматизации Atlas следует использовать официальный Maven-репозиторий NeoForged и проверять существование installer JAR и контрольной суммы перед началом установки.

## Требование Atlas

Редактор профиля не должен позволять ручной произвольный ввод версии loader. Для Forge, NeoForge, Fabric и Quilt он обязан показывать отдельный список версий, обновляемый после выбора Minecraft, и сохранять выбранную совместимую версию в `LoaderSpec::version`.

## Ожидаемые официальные адреса для проверки реализации

- Forge Maven metadata: <https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml>
- NeoForge Maven metadata: <https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml>
- Fabric launcher metadata: <https://meta.fabricmc.net/v2/versions/loader/{minecraftVersion}>
- Quilt launcher metadata: <https://meta.quiltmc.org/v3/versions/loader/{minecraftVersion}>

Перед показом версии в UI Atlas должен дополнительно проверять URL installer и checksum для Forge / NeoForge, а для Fabric / Quilt — официальный launcher profile endpoint.

[Источник NeoForge: официальный сайт](https://neoforged.net/)

## Forge

- Официальная страница: <https://files.minecraftforge.net/>
- Проверено 12 августа 2026: официальный сайт показывает выбор версии Minecraft и для выбранной версии — `Latest`, `Recommended`, список всех версий и installer JAR из `maven.minecraftforge.net`.
- Пример текущего формата версии, показанный официальной страницей: для Minecraft `26.2` — Forge `26.2-65.1.1`; следовательно, совместимость Forge можно безопасно определять по префиксу `<minecraftVersion>-` в Maven metadata и подтверждать существованием HTTPS installer JAR с SHA-1.

[Источник Forge: официальная страница загрузок](https://files.minecraftforge.net/)

## Проверка из Atlas (LoaderVersionProbe)

После обновления XML/JSON-разбора нативная проверка `LoaderVersionProbe` запросила официальные источники через тот же `LoaderInstallService`, который использует интерфейс Atlas.

| Загрузчик | Версия Minecraft | Результат |
|---|---:|---|
| Fabric | 1.20.1 | 251 совместимая версия; первая в ответе: `0.19.3` |
| Quilt | 1.20.1 | 299 совместимых версий; первая в ответе: `0.20.0-beta.9` |
| Forge | 1.20.1 | 131 совместимая версия; первая в ответе: `1.20.1-47.4.22` |
| NeoForge | 1.20.2 | 79 совместимых версий; первая в ответе: `20.2.93` |

NeoForge не выпускался для Minecraft 1.20.1. Поэтому интерфейс не должен придумывать вариант NeoForge для 1.20.1: он обязан сообщить, что совместимых версий не найдено, и предложить сменить Minecraft или загрузчик.

