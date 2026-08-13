# Quality audit — Atlas Launcher 0.3.3

## Проверка от 2026-08-13

Базовая нативная сборка и текущие встроенные тесты завершились успешно: `PackageServiceTests`, `OfflineNicknameTests`, `LoaderVersionProbe` и `MinecraftVersionsRefreshProbe` — 4 из 4.

Графический запуск в изолированном режиме `QT_QPA_PLATFORM=offscreen` успешно инициализировал главное окно и сервисы без аварийного завершения. Предупреждение об отсутствующем `AtlasUpdater.exe` относится к нативной Linux-проверке: обновитель упаковывается только в Windows-релиз.

## Подтверждённые дефекты

| Приоритет | Проблема | Влияние |
|---|---|---|
| Критический | Редактор профиля не предлагал Java 16, хотя Minecraft 1.17 требует именно её. | 1.17 нельзя было корректно настроить через локальную Java Atlas. |
| Критический | Официальный Adoptium endpoint для Windows `image_type=jre` вернул пустой массив для Java 16, тогда как `image_type=jdk` вернул валидный архив. | Автоматическая установка Java для Minecraft 1.17 завершалась ошибкой. |
| Высокий | Версии Alpha/Beta (`a1.*`, `b1.*`, `inf-*`, `rd-*`, `c*`) считались неизвестными и получали Java 21. | Старые версии не запускались с подходящей Java 8. |
| Высокий | Запуск игнорировал сохранённое поле `managedMajor` у профиля и всегда выбирал вычисленную Java. | Явный выбор локальной Java Atlas в профиле не соблюдался. |

## Источник Java Runtime

Проверка выполнялась по официальному API Adoptium: <https://api.adoptium.net/v3/assets/latest/16/hotspot?architecture=x64&image_type=jre&os=windows&vendor=eclipse> вернул `[]`, а запрос с `image_type=jdk` вернул валидный пакет. Для отсутствующих JRE Atlas должен повторить запрос как JDK того же major; JDK содержит `javaw.exe` и подходит для запуска Minecraft.

## UX-наблюдения

Интерфейс не падает на старте и уже защищён от горизонтальной прокрутки, но ему требуется более ясная визуальная иерархия: убрать дублирование бренда, усилить контраст статусов и кнопок, сделать карточки и настройки менее тяжёлыми, а действия с Java — понятнее. Эти изменения выполняются без тяжёлых эффектов и без отказа от Windows 7.

## Quilt Metadata API: задержка списка загрузчиков

Официальная документация Quilt Meta указывает endpoint `GET /v3/versions/loader/{game_version}` для списка loader-версий: <https://meta.quiltmc.org/>.

При проверке 13 августа 2026 года запрос `https://meta.quiltmc.org/v3/versions/loader/1.20.1` ответил `HTTP 200`, но размер JSON составил **870479 байт** и в данной среде передавался медленнее 35 секунд; за это время было получено примерно 336 КБ. Путь API и формат ответа корректны, поэтому проблему нельзя решать отключением Quilt. В Atlas добавлен лимит времени сетевого запроса, а список версий следует кэшировать в пределах работы приложения, чтобы не повторять большой запрос при повторном выборе тех же Minecraft и loader.

Источник: [Quilt Meta API](https://meta.quiltmc.org/).

### Legacy assets для Beta и Alpha

Официальный asset index `pre-1.6` для Beta 1.7.3 содержит флаг `"map_to_resources": true` (проверен 13 августа 2026 года по `https://launchermeta.mojang.com/v1/packages/3d8e55480977e32acd9844e545177e69a52f594b/pre-1.6.json`). Значит после загрузки объектов лаунчер обязан восстановить файлы по их логическим путям в каталоге `resources` конкретного экземпляра, иначе pre-1.6 не получает звуки и прочие legacy-ресурсы.

Проверенная референсная реализация Prism Launcher: [`AssetsUtils.cpp`](https://raw.githubusercontent.com/PrismLauncher/PrismLauncher/d909e0205d940cb2846fdab665aa3c69015303af/launcher/minecraft/AssetsUtils.cpp). Она читает `virtual` и `map_to_resources`; при `virtual` использует `assets/virtual/<asset-index-id>`, а при `map_to_resources` восстанавливает объекты в переданный `resourcesFolder`. В Atlas `--gameDir` уже указывает на `Instance::rootPath`, поэтому корректная цель для `map_to_resources` — `<instance.rootPath>/resources`.

Источник проверен только для совместимости поведения: код Prism Launcher не копируется в Atlas.

## Проверка Minecraft 1.7.10

13 августа 2026 года выполнена чистая интеграционная установка Vanilla Minecraft 1.7.10 через `VanillaPipelineProbe` в отдельный каталог `/tmp/atlas-1710-pipeline`. Официальные metadata, client JAR, libraries, Windows natives и assets были получены обычной очередью `DownloadManager`; сервис завершился сигналом `install finished for 1.7.10; completed tasks: 722`. В папке версии созданы `1.7.10.json` и `1.7.10.jar`.

Проверка подтверждает работу legacy-цепочки: Maven fallback для старого формата библиотек, исключение отсутствующего базового JAR у native-only entries и безопасную отмену очереди при ошибках.
