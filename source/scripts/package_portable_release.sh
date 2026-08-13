#!/usr/bin/env bash
# Creates the distributable Windows portable archive from an already deployed
# Qt runtime and the cross-compiled Atlas binaries. Run from Linux after the
# Windows build and after scripts/deploy_windows.ps1 (or an equivalent deploy).
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="${1:-0.3.3}"
dist_dir="$repo_root/dist"
portable_dir="$dist_dir/AtlasLauncher-win64"
staging_root="$dist_dir/.AtlasLauncher-win64-staging"
staging_dir="$staging_root/AtlasLauncher-win64"
build_dir="$repo_root/build-win64"
openssl_dir="$repo_root/third_party/openssl-win64"
archive="$dist_dir/AtlasLauncher-${version}-win64-portable.zip"

require_file() {
    local file="$1"
    if [[ ! -f "$file" ]]; then
        printf 'Missing required release file: %s\n' "$file" >&2
        exit 1
    fi
}

require_dir() {
    local directory="$1"
    if [[ ! -d "$directory" ]]; then
        printf 'Missing required release directory: %s\n' "$directory" >&2
        exit 1
    fi
}

require_dir "$portable_dir"
require_file "$build_dir/AtlasLauncher.exe"
require_file "$build_dir/AtlasUpdater.exe"
require_file "$build_dir/TlsRuntimeProbe.exe"
require_file "$openssl_dir/bin/libcrypto-1_1-x64.dll"
require_file "$openssl_dir/bin/libssl-1_1-x64.dll"
require_file "$openssl_dir/LICENSE.txt"
require_file "$openssl_dir/VERSION.txt"
require_file "$openssl_dir/SHA256SUMS.txt"
require_file "$repo_root/docs/PORTABLE_README.txt"
require_file "$repo_root/docs/THIRD_PARTY_NOTICES.txt"
require_file "$repo_root/installer/AtlasLauncher-Portable-Setup.iss"

rm -rf "$staging_root"
mkdir -p "$staging_dir"
cp -a "$portable_dir/." "$staging_dir/"

cp -f "$build_dir/AtlasLauncher.exe" "$staging_dir/AtlasLauncher.exe"
cp -f "$build_dir/AtlasUpdater.exe" "$staging_dir/AtlasUpdater.exe"
cp -f "$build_dir/TlsRuntimeProbe.exe" "$staging_dir/AtlasTLSProbe.exe"
cp -f "$openssl_dir/bin/libcrypto-1_1-x64.dll" "$staging_dir/libcrypto-1_1-x64.dll"
cp -f "$openssl_dir/bin/libssl-1_1-x64.dll" "$staging_dir/libssl-1_1-x64.dll"
cp -f "$repo_root/docs/PORTABLE_README.txt" "$staging_dir/README.txt"
cp -f "$repo_root/docs/THIRD_PARTY_NOTICES.txt" "$staging_dir/THIRD_PARTY_NOTICES.txt"
cp -f "$repo_root/installer/AtlasLauncher-Portable-Setup.iss" "$staging_dir/AtlasLauncher-Portable-Setup.iss"
mkdir -p "$staging_dir/licenses/OpenSSL-1.1.1w"
cp -f "$openssl_dir/LICENSE.txt" "$staging_dir/licenses/OpenSSL-1.1.1w/"
cp -f "$openssl_dir/VERSION.txt" "$staging_dir/licenses/OpenSSL-1.1.1w/"
cp -f "$openssl_dir/SHA256SUMS.txt" "$staging_dir/licenses/OpenSSL-1.1.1w/"

for file in \
    AtlasLauncher.exe \
    AtlasUpdater.exe \
    AtlasTLSProbe.exe \
    Qt5Core.dll \
    Qt5Gui.dll \
    Qt5Network.dll \
    Qt5Widgets.dll \
    libgcc_s_seh-1.dll \
    libstdc++-6.dll \
    libwinpthread-1.dll \
    libcrypto-1_1-x64.dll \
    libssl-1_1-x64.dll \
    platforms/qwindows.dll \
    tools/7za.exe \
    tools/7za.dll \
    tools/7zip-LICENSE.txt \
    tools/7zip-SHA256.txt \
    AtlasLauncher-Portable-Setup.iss \
    licenses/OpenSSL-1.1.1w/LICENSE.txt \
    licenses/OpenSSL-1.1.1w/VERSION.txt \
    licenses/OpenSSL-1.1.1w/SHA256SUMS.txt
    do
    require_file "$staging_dir/$file"
done

rm -rf "$portable_dir"
mv "$staging_dir" "$portable_dir"
rmdir "$staging_root"
rm -f "$archive"
(
    cd "$dist_dir"
    zip -q -r "$(basename "$archive")" "$(basename "$portable_dir")"
)
unzip -tq "$archive" >/dev/null

printf 'Created: %s\n' "$archive"
printf 'SHA-256: '
sha256sum "$archive" | awk '{print $1}'
