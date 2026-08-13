# GitHub Releases API for Atlas Updater

The updater will query the public endpoint `GET https://api.github.com/repos/{owner}/{repo}/releases/latest` for a configured GitHub repository. The response includes the published release `tag_name` and an `assets` list. Each asset provides `name`, `browser_download_url`, `size`, and optionally `digest`.

Atlas will accept a release only when it has an expected `AtlasLauncher-<version>-win64-portable.zip` asset and a `SHA256SUMS.txt` asset. It will download the ZIP through the public `browser_download_url`, compare it to the matching SHA-256 value in `SHA256SUMS.txt`, and only then unpack and replace files through the separate updater executable.

Use HTTP headers `Accept: application/vnd.github+json`, a non-empty `User-Agent`, and `X-GitHub-Api-Version: 2026-03-10`. Public release metadata and download URLs require no embedded user token. The GitHub repository is configured as an `owner/repository` value in Atlas settings so a project owner can select their own release channel.

## Sources

1. [GitHub REST API — Releases](https://docs.github.com/en/rest/releases/releases#get-the-latest-release)
2. [GitHub REST API — Release assets](https://docs.github.com/en/rest/releases/assets)
