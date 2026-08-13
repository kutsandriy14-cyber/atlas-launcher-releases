# Параметры каталога: подтверждённые API

## Modrinth

Источник: [Search projects](https://docs.modrinth.com/api/operations/searchprojects/), извлечено 2026-08-13.

`GET https://api.modrinth.com/v2/search` поддерживает:

- `query` — текстовый запрос;
- `facets` — JSON-массив групп: элементы в одной группе означают OR, разные группы — AND;
- основные facets: `project_type`, `categories`, `versions`, `environment`;
- `index`: `relevance`, `downloads`, `follows`, `newest`, `updated`;
- `offset` и `limit` (до 100) для постраничной выдачи.

Категории получает `GET https://api.modrinth.com/v2/tag/category`; объект содержит `name`, `project_type` и `header`.

## CurseForge

Источник: [CurseForge for Studios REST API](https://docs.curseforge.com/rest-api/), извлечено 2026-08-13.

`GET https://api.curseforge.com/v1/mods/search` поддерживает:

- обязательный `gameId`;
- `classId`, `categoryId`/`categoryIds`;
- `gameVersion`/`gameVersions`;
- `searchFilter`;
- `sortField`, `sortOrder`;
- `modLoaderType`/`modLoaderTypes` (требует совместного `gameVersion`);
- `pageSize`, `index`.

Значения `ModsSearchSortField`: 1 Featured, 2 Popularity, 3 LastUpdated, 4 Name, 5 Author, 6 TotalDownloads, 7 Category, 8 GameVersion, 9 EarlyAccess, 10 FeaturedReleased, 11 ReleasedDate, 12 Rating.

`GET https://api.curseforge.com/v1/categories?gameId=432&classId={classId}` отдаёт Minecraft-классы и категории. Максимальный размер страницы поиска — 50; ограничение пагинации — до 10 000 результатов.

## Правило приватности

CurseForge API-ключ остаётся только в оперативной памяти процесса. Он не должен попадать в `LauncherSettings`, JSON-файлы, логи, исходники, URL или релизные артефакты.

## Проверка живого запроса Modrinth — 2026-08-13

Независимый веб-канал успешно выполнил официальный запрос `GET /v2/search` для `query=sodium`, `limit=5`, `offset=0`, `index=downloads` и facets `[["project_type:mod"],["versions:1.20.1"],["categories:fabric"]]`.

Ответ содержал `total_hits: 35` и, в частности, проекты Sodium и Sodium Extra. Это подтверждает, что сочетание фильтров типа контента, Minecraft 1.20.1, Fabric, сортировки по скачиваниям и страницы 1 поддерживается официальным API. Локальный `curl` в сброшенной среде получил `SSL_ERROR_SYSCALL`, но это ограничение TLS среды; запрос получен другим сетевым каналом.

Источник: https://api.modrinth.com/v2/search?query=sodium&limit=5&offset=0&index=downloads&facets=%5B%5B%22project_type%3Amod%22%5D%2C%5B%22versions%3A1.20.1%22%5D%2C%5B%22categories%3Afabric%22%5D%5D
