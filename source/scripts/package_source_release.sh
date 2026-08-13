#!/usr/bin/env bash
# Creates a clean source archive for an Atlas Launcher release. Build products,
# release archives and compiled OpenSSL runtime DLLs are intentionally excluded.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="${1:-0.2.7}"
dist_dir="$repo_root/dist"
base_name="AtlasLauncher-${version}-source"
staging_root="$dist_dir/.source-staging"
staging_dir="$staging_root/$base_name"
archive="$dist_dir/${base_name}.zip"

rm -rf "$staging_root"
mkdir -p "$staging_dir"
(
    cd "$repo_root"
    # The release source archive must contain only tracked files from HEAD.
    # This prevents local build trees, downloaded test data and release artifacts
    # from being accidentally published.
    git archive --format=tar HEAD | tar -xf - -C "$staging_dir"
)

for file in \
    CMakeLists.txt \
    README.md \
    scripts/build_openssl_win64.sh \
    scripts/deploy_windows.ps1 \
    scripts/package_portable_release.sh \
    installer/AtlasLauncher.iss \
    src/main.cpp \
    tests/tls_runtime_probe.cpp \
    docs/TLS_AUDIT.md \
    third_party/openssl-win64/LICENSE.txt \
    third_party/openssl-win64/VERSION.txt \
    third_party/openssl-win64/SHA256SUMS.txt
    do
    if [[ ! -f "$staging_dir/$file" ]]; then
        printf 'Source archive staging is missing: %s\n' "$file" >&2
        exit 1
    fi
done

rm -f "$archive"
(
    cd "$staging_root"
    zip -q -r "$(basename "$archive")" "$base_name"
)
mv "$staging_root/$(basename "$archive")" "$archive"
unzip -tq "$archive" >/dev/null
if unzip -Z1 "$archive" | grep -qE '(^|/)(build|build-win64|dist|test-output|smoke-data|third_party/openssl-win64/bin)(/|$)'; then
    printf 'Source archive contains an excluded build or binary runtime directory.\n' >&2
    exit 1
fi
rm -rf "$staging_root"

printf 'Created: %s\n' "$archive"
printf 'SHA-256: '
sha256sum "$archive" | awk '{print $1}'
