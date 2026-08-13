#!/usr/bin/env bash
# Builds the runtime OpenSSL DLLs required by the Qt 5.15 MinGW package.
# The output is deliberately kept outside dist; deploy_windows.ps1 copies only
# the checked runtime files into a portable package.
set -euo pipefail

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
VERSION="1.1.1w"
ARCHIVE="openssl-${VERSION}.tar.gz"
SOURCE_URL="https://github.com/openssl/openssl/releases/download/OpenSSL_1_1_1w/${ARCHIVE}"
# Published next to the official release archive by the OpenSSL project.
EXPECTED_SHA256="cf3098950cb4d853ad95c0841f1f9c6d3dc102dccfcacd521d93925208b76ac8"
WORK_DIR="${ROOT_DIR}/.build/openssl-${VERSION}-win64"
ARCHIVE_PATH="${WORK_DIR}/${ARCHIVE}"
SOURCE_DIR="${WORK_DIR}/openssl-${VERSION}"
OUTPUT_DIR="${ROOT_DIR}/third_party/openssl-win64"
PREFIX_DIR="${WORK_DIR}/install"

for command in curl sha256sum tar perl make x86_64-w64-mingw32-gcc; do
    command -v "$command" >/dev/null || {
        echo "Missing required command: $command" >&2
        exit 1
    }
done

mkdir -p "$WORK_DIR"
if [[ ! -f "$ARCHIVE_PATH" ]]; then
    curl --fail --location --retry 3 --proto '=https' --tlsv1.2 \
        "$SOURCE_URL" --output "$ARCHIVE_PATH"
fi
printf '%s  %s\n' "$EXPECTED_SHA256" "$ARCHIVE_PATH" | sha256sum --check --status

rm -rf "$SOURCE_DIR" "$PREFIX_DIR" "$OUTPUT_DIR"
tar -xzf "$ARCHIVE_PATH" -C "$WORK_DIR"

pushd "$SOURCE_DIR" >/dev/null
# no-asm avoids an additional assembler dependency; it does not change TLS
# protocol support and makes this Windows 7 x64 build reproducible.
perl Configure mingw64 shared no-tests no-asm \
    --cross-compile-prefix=x86_64-w64-mingw32- \
    --prefix="$PREFIX_DIR" \
    --openssldir="$PREFIX_DIR/ssl" \
    '-static-libgcc'
make -j"$(nproc)"
make install_sw
popd >/dev/null

mkdir -p "$OUTPUT_DIR/bin"
for dll in "$PREFIX_DIR/bin/libcrypto-1_1-x64.dll" "$PREFIX_DIR/bin/libssl-1_1-x64.dll"; do
    [[ -f "$dll" ]] || { echo "Expected DLL was not produced: $dll" >&2; exit 1; }
    cp "$dll" "$OUTPUT_DIR/bin/"
done
cp "$SOURCE_DIR/LICENSE" "$OUTPUT_DIR/LICENSE.txt"
printf '%s\n' "$VERSION" > "$OUTPUT_DIR/VERSION.txt"
(
    cd "$OUTPUT_DIR"
    sha256sum bin/libcrypto-1_1-x64.dll bin/libssl-1_1-x64.dll LICENSE.txt VERSION.txt > SHA256SUMS.txt
)

echo "OpenSSL runtime prepared in: $OUTPUT_DIR"
