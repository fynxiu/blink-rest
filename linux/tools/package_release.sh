#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: linux/tools/package_release.sh VERSION [DIST_DIR]" >&2
  exit 2
}

[[ $# -ge 1 && $# -le 2 ]] || usage
VERSION="$1"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || usage

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DIST_DIR="$ROOT/dist"
if [[ $# -eq 2 ]]; then DIST_DIR="$2"; fi
mkdir -p "$DIST_DIR"
DIST_DIR="$(cd "$DIST_DIR" && pwd)"
ARCH="$(dpkg --print-architecture)"
case "$ARCH" in
  amd64|arm64) ;;
  *) echo "Unsupported release architecture: $ARCH" >&2; exit 2 ;;
esac
TAG="v$VERSION"
STAGING="$(mktemp -d)"
trap 'rm -rf "$STAGING"' EXIT

cmake -S "$ROOT/linux" -B "$STAGING/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DBLINKREST_VERSION="$VERSION"
cmake --build "$STAGING/build" --parallel
ctest --test-dir "$STAGING/build" --output-on-failure

mkdir -p "$STAGING/root"
DESTDIR="$STAGING/root" cmake --install "$STAGING/build"
tar -C "$STAGING/root" -czf "$DIST_DIR/BlinkRest-$TAG-linux-$ARCH.tar.gz" usr

mkdir -p "$STAGING/debs"
cpack --config "$STAGING/build/CPackConfig.cmake" -G DEB -B "$STAGING/debs"
DEB="$(find "$STAGING/debs" -maxdepth 1 -type f -name '*.deb' -print -quit)"
[[ -n "$DEB" && -f "$DEB" ]] || { echo "Expected a .deb" >&2; exit 1; }
cp "$DEB" "$DIST_DIR/BlinkRest-$TAG-linux-$ARCH.deb"

(cd "$DIST_DIR" && sha256sum "BlinkRest-$TAG-linux-$ARCH.deb" \
  "BlinkRest-$TAG-linux-$ARCH.tar.gz" > "SHA256SUMS-linux-$ARCH.txt")
echo "Linux assets ready in $DIST_DIR for $ARCH"
