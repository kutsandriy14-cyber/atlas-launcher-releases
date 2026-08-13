# CurseForge integration notes

## Official sources

- CurseForge REST API documentation: https://docs.curseforge.com/rest-api/
- CurseForge third-party API application guidance: https://support.curseforge.com/support/solutions/articles/9000208346-about-the-curseforge-api-and-how-to-apply-for-a-key
- CurseForge for Studios console: https://console.curseforge.com/

## Confirmed implementation details

CurseForge REST API uses the `https://api.curseforge.com` base URL and authenticates requests with the `x-api-key` HTTP header. The current Atlas client calls the documented Minecraft mod search endpoint (`GET /v1/mods/search`) with `gameId=432` and resolves a selected file through `GET /v1/mods/{modId}/files/{fileId}`.

The Console UI explains that an organization API key grants access to CurseForge public game repositories. The key displayed by the Console may use a string format that resembles a bcrypt value (for example, a `$2a$...` prefix); when copied from the Console it must be passed verbatim as the complete `x-api-key` value. It is not an Atlas password hash.

## Security model for Atlas

The CurseForge API key must be accepted only in a password-masked input field, kept only in the running process memory, transmitted only as the HTTPS `x-api-key` request header, and excluded from `settings.json`, logs, source control, archives, installers, and releases. Do not place a user key into test commands, test files, screenshots, or project configuration.

## Product scope

Atlas should support search and selection of compatible public Minecraft content, then install only the explicitly selected, HTTPS-hosted file after hash validation into the selected local instance. A CurseForge modpack import must be treated separately from a single-file mod install and requires its manifest/archive workflow.

## Compatibility selection

The official Search Mods response schema contains a `latestFilesIndexes` list. Each index item carries `gameVersion`, `fileId`, `filename`, and `modLoader`. Atlas should choose a file from this list matching the selected local instance's Minecraft version (and, for mods, its loader where the API reports one) instead of blindly using `mainFileId`. The Search Mods endpoint accepts the documented `gameId`, `searchFilter`, `gameVersion`, `classId`, `pageSize`, and `index` query parameters.

Source: https://docs.curseforge.com/rest-api/#search-mods
