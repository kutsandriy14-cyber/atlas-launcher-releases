# API Sources and Implementation Notes

This document records external API requirements used in Atlas Launcher.

## Modrinth

The public production API is served from `https://api.modrinth.com`. Most read requests do not need a token, but every client must send a uniquely identifying `User-Agent`. The documented rate limit is 300 requests per minute per IP, and the response exposes `X-Ratelimit-Limit`, `X-Ratelimit-Remaining` and `X-Ratelimit-Reset` headers. The launcher sends a configurable `AtlasLauncher/0.1.0 (personal launcher)` user agent and converts HTTP 429 into a visible retry message.

The project search route is `GET /v2/search`. It accepts a textual `query`, a JSON-encoded `facets` parameter, `index`, `offset`, and `limit` (1–100). Within a facet group terms are ORed; separate groups are ANDed. Atlas uses the documented `versions:<version>`, `categories:<loader>` and `project_type:<type>` facets. Search result records contain the project ID, type, title, description, author, categories, supported versions, download count, icon URL and latest version ID.

The version route is `GET /v2/project/{id|slug}/version`. It permits `loaders`, `game_versions`, `featured`, and `include_changelog` filters. Version files include direct URL, filename, size, SHA-1/SHA-512 hashes and primary-file marker. This is the planned source for the download queue and integrity verification.

Sources:

- https://docs.modrinth.com/api/ — API overview, authentication, rate limits, user agents.
- https://docs.modrinth.com/api/operations/searchprojects/ — search route, facet semantics and hit schema.
- https://docs.modrinth.com/api/operations/getprojectversions/ — version route, loader/game-version filters and file hashes.

## CurseForge

The documented REST base URL is `https://api.curseforge.com`; requests use the `x-api-key` header. CurseForge states that a third-party service must apply for a key. The terms state that API keys are unique, non-transferable, must not be shared or disclosed to third parties, and that API materials must not be cached. Atlas therefore does not embed a key in source or binary. A future adapter can only operate after the user has entered a personally issued key and agreed to the provider's current terms.

Sources:

- https://docs.curseforge.com/rest-api/ — REST API base URL and `x-api-key` authentication.
- https://support.curseforge.com/en/support/solutions/articles/9000207405-curse-forge-3rd-party-api-terms-and-conditions — external-app terms and API key restrictions.
