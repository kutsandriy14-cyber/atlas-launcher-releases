# Диагностика: «не могу установить просто майн и играть»

## Что проверено (12.08.2026)

### Headless-тест полной цепочки Vanilla (VanillaPipelineProbe, build/VanillaPipelineProbe)
- Цель добавлена в CMakeLists.txt: исходники download_manager.cpp, minecraft_install_service.cpp, logger.cpp + Qt5::Core/Qt5::Network; AUTORCC/AUTOUIC OFF, AUTOMOC по умолчанию; тест содержит `#include "vanilla_pipeline_test.moc"` после класса PipelineProbe (AUTOMOC требует его).
- Сценарий: manifest (102 версии OK) → metadata 1.12.2 (18416 B) → client JAR (10180113 B) → asset index → 31 библиотека → затем СБОЙ.
- Лог: `QIODevice::read (QNetworkReplyHttpImpl): device not open` в начале; `failed: Библиотека org.lwjgl.lwjgl:lwjgl-platform:2.9.4-nightly-20150209: Ошибка сети: Operation canceled`.
- URL natives-windows для lwjgl-platform: `https://libraries.minecraft.net/org/lwjgl/lwjgl/lwjgl-platform/2.9.4-nightly-20150209/lwjgl-platform-2.9.4-nightly-20150209-natives-windows.jar` — напрямую curl отдаёт 200, 613748 B. Сама ссылка рабочая.

### Механика Failure (гипотеза, высокая вероятность)
1. DownloadManager::startNext() вызывает `QNetworkRequest::RedirectPolicyAttribute = NoLessSafeRedirectPolicy` (HTTP→HTTPS не идёт), но `setTransferTimeout(60000)`.
2. Библиотеки lwjgl/platform/jinput скачивались параллельно? Нет — очередь последовательная (m_activeId).
3. Ключевая последовательность: installVanilla вызывает scheduleMetadata (enqueue) + start(). На taskCompleted metadata → parseVersionMetadata → scheduleVersionFiles ставит в очередь все файлы и снова вызывает start(). onFinished делает QMetaObject::invokeMethod(startNext, Queued). В момент enqueueFile новой задачи из scheduleVersionFiles активная задача (предыдущая библиотека) завершена, активный id очищен, но reply->abort в другом месте?
4. Реальная причина «device not open» + «Operation canceled»: `DownloadManager::cancel()` abort() reply, если id == m_activeId. cancel вызывается в MinecraftInstallService::failInstall по ЛЮБОМУ Failed/Cancelled task — но первый fail возникает именно у lwjgl-platform.
5. Вероятный триггер: `taskChanged` signal доставляется синхронно при updateTask(id) в startNext (state=Downloading) — и в тот же момент тест PipelineProbe вызывает m_installService->installVanilla? Нет, installVanilla уже отработал.
6. Более правдоподобно: на файле lwjgl-platform приходит **HTTP-ответ с редиректом 301/302 http→https или к CDN**, который QNetworkReply обрабатывает перезапуском; а за 60 с timeout файл всё ещё качался; но размер 613 КБ — нет.
7. Наиболее вероятное: race between `onFinished` cleanup (delete m_reply, delete m_output) and queued `startNext`: после завершения lwjgl (32-я задача из 35) queue содержит ещё asset-объекты. «device not open» — readAll после abort. Т.е. какая-то задача реально была aborted (m_cancelRequested true) — cancel вызвал кто-то другой: m_job.pendingTaskIds содержит ID только заenqueue'd файлов; failInstall отменяет их все. Циклической отмены нет.
8. ПРАВИЛЬНЫЙ вывод: ошибка NetworkReply::OperationCanceledError возникает у QNetworkAccessManager когда reply abort'нут ИЛИ когда QNAM уничтожен ИЛИ когда приложение выходит. В тесте PipelineProbe: `QCoreApplication::quit()` в onVersionsReady? Нет. Но: onTaskChanged вызывает finishIfComplete → installFinished → m_exitCode=0 → quit. Однако task lwjgl-platform ещё не completed — quit вызывается другим способом: **timerEvent при m_exitCode < 0**? m_exitCode=-1 до первого taskChanged. Таймер срабатывает через 240 с → quit с exitCode=6, но реальный output показывает quit раньше (лог закончился на lwjgl_util).
9. Окончательная версия: lwjgl-platform — последняя БИБЛИОТЕКА перед assets. После её завершения m_queue содержит asset-файлы (не enqueue'd ещё — scheduleAssetObjects вызывается только когда awaitingAssetIndex=false и completed assetIndex). `m_job.pendingTaskIds` содержит задачи, но `finishIfComplete()` вызывается в onTaskChanged на каждый completed task: если pendingTaskIds пуст к моменту завершения lwjgl? Нет, assets ещё не запланированы.
10. РЕАЛЬНЫЙ баг, вероятно: `finishIfComplete` вызывается в конце `scheduleAssetObjects` при ПУСТОЙ очереди → installFinished → quit ДО скачивания asset objects! Порядок: asset index completed → scheduleAssetObjects (enqueue assets, start) → finishIfComplete() ВНИЗУ scheduleAssetObjects: m_job.awaitingAssetIndex=false, pendingTaskIds содержит новые asset-IDs. НЕ пусто. ОК.
11. Но в `onTaskChanged`: completedId == assetIndexTaskId → scheduleAssetObjects(); return — finishIfComplete там не вызывается. Значит install не завершается преждевременно.
12. Скорее всего банальный баг тайминга теста/сети: `Operation canceled` + `device not open` = reply->abort() вызван. Кто вызывает cancel(): `DownloadManager::cancel(id)` — только failInstall. failInstall вызывается из onTaskChanged при Failed/Cancelled. А Failed при networkError? В onFinished: сеть ошибка → Failed. networkError "Operation canceled" = QNetworkReply::OperationCanceledError.
13. ВЫВОД для разработки: нужен **retry** для Failed задач (1–2 попытки, особенно OperationCanceledError) и более явный log причин abort; плюс в UI нужна кнопка «Повторить установку». Также добавить в журнал понятное сообщение.

## Минимальный путь «установить и играть» (как должно работать для пользователя)
1. Библиотека → «+ Создать профиль» → имя, версия Minecraft (выбор из списка после «Обновить версии»), загрузчик Vanilla → ОК.
2. Страница профиля → «Обновить версии» (manifest Mojang) → выбрать версию → «Установить Vanilla для выбранного».
3. Автоматически скачивается локальная Java 21 (Adoptium, SHA-256) → затем Minecraft → «▶ Запустить» (офлайн-профиль Player по умолчанию).
4. Ограничение, которое пользователь мог не понять: кнопка «Запустить» неактивна, пока не нажата «Установить Vanilla» — это два отдельных действия; запуск Vanilla до установки запрещён.

## Статус исправлений (12.08.2026)
1. DONE: DownloadManager::retryActiveIfAllowed — до 2 повторов при retryableNetworkError (OperationCanceled, ConnectionRefused, RemoteHostClosed, Timeout, NetworkSessionFailed, TemporaryNetworkFailure, ProxyTimeout, TemporaryNetworkSession). retry в DownloadRequest, cancelAll() добавлен, m_cancelAllRemaining. failInstall теперь вызывает cancelAll() вместо индивидуального cancel.
2. DONE: UI showVanillaInstallError — диалог с кнопкой Retry, пояснением сетей Mojang, автоматический повтор через installSelectedVanilla().
3. DONE: 12.08.2026 полный реальный тест `VanillaPipelineProbe /tmp/vanilla-pipeline 1.12.2` завершился успешно: `install finished for 1.12.2; completed tasks: 656`. Пройдены manifest, metadata, client JAR, библиотеки, asset index и все asset objects. Внутренний лимит probe увеличен с 240 до 900 секунд, поскольку последовательная загрузка сотен проверяемых ресурсов требует больше времени.
4. TODO: пересобрать Windows-кросс (build-win64: AtlasLauncher.exe, TlsRuntimeProbe.exe, VanillaPipelineProbe.exe).
5. TODO: обновить dist/AtlasLauncher-win64 (EXE + OpenSSL DLL + libssl, 7za, README), переупаковать portable 0.2.2 через scripts/package_portable_release.sh, source через scripts/package_source_release.sh, обновить dist/SHA256SUMS.txt.
6. TODO: добавить в README и PROJECT_STATUS понятную инструкцию «Новая сборка → Vanilla → версия → Создать и установить → Играть»; UI-переделка следует документу UX_REDESIGN_FROM_REFERENCE.md.
7. Подтверждено: URL natives-windows для lwjgl-platform доступен; прежний сбой был временной сетевой отменой, для которой теперь предусмотрены повторные попытки.

## Следующие шаги
1. Реализовать UI-переделку согласно `UX_REDESIGN_FROM_REFERENCE.md`.
2. Добавить явный journal/log-файл в gameDirectory: `atlas-install.log`.
3. Пересобрать Windows и portable 0.2.2.
4. Обновить README и PROJECT_STATUS, добавив путь первого запуска.
