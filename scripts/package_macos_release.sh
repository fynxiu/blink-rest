#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/package_macos_release.sh VERSION [DIST_DIR]

Builds, verifies, and packages Blink Rest for macOS arm64 and x86_64.
It does not create tags, push commits, or create GitHub releases.
EOF
}

die() {
    echo "package-macos: $*" >&2
    exit 1
}

VERSION="${1:-}"
[[ -n "$VERSION" ]] || {
    usage >&2
    exit 2
}
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "VERSION must use X.Y.Z semantic-version form"

TAG="v$VERSION"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${2:-$ROOT/dist/$TAG}"
PROJECT="$ROOT/BlinkRest.xcodeproj"
SCHEME="BlinkRest"
APP_NAME="BlinkRest.app"
EXECUTABLE_NAME="BlinkRest"
TMP_ROOT="${TMPDIR:-/private/tmp}"
WORK_DIR="$(mktemp -d "$TMP_ROOT/blinkrest-macos-release.XXXXXX")"
BUILD_NUMBER="$(git -C "$ROOT" rev-list --count HEAD)"

cleanup() {
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

for command_name in git xcodebuild codesign ditto lipo; do
    command -v "$command_name" >/dev/null 2>&1 || die "required command not found: $command_name"
done
[[ -x /usr/libexec/PlistBuddy ]] || die "required command not found: /usr/libexec/PlistBuddy"

mkdir -p "$DIST_DIR"

xcodebuild test -quiet \
    -project "$PROJECT" \
    -scheme "$SCHEME" \
    -configuration Debug \
    -destination 'platform=macOS' \
    -derivedDataPath "$WORK_DIR/DerivedData-tests" \
    CODE_SIGNING_ALLOWED=NO \
    -only-testing:BlinkRestTests

build_arch() {
    local arch="$1"
    local derived="$WORK_DIR/DerivedData-$arch"
    local app="$derived/Build/Products/Release/$APP_NAME"
    local zip="$DIST_DIR/BlinkRest-$TAG-macos-$arch.zip"

    xcodebuild -quiet \
        -project "$PROJECT" \
        -scheme "$SCHEME" \
        -configuration Release \
        -destination 'generic/platform=macOS' \
        -derivedDataPath "$derived" \
        CODE_SIGNING_ALLOWED=NO \
        ARCHS="$arch" \
        ONLY_ACTIVE_ARCH=NO \
        MARKETING_VERSION="$VERSION" \
        CURRENT_PROJECT_VERSION="$BUILD_NUMBER" \
        clean build

    codesign --force --deep --sign - "$app"
    codesign --verify --deep --strict "$app"

    local actual_version
    local actual_archs
    actual_version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")"
    [[ "$actual_version" == "$VERSION" ]] || die "$arch app version is $actual_version, expected $VERSION"
    actual_archs="$(lipo -archs "$app/Contents/MacOS/$EXECUTABLE_NAME")"
    [[ " $actual_archs " == *" $arch "* ]] || die "$arch build has unexpected architecture(s): $actual_archs"

    ditto -c -k --sequesterRsrc --keepParent "$app" "$zip"
    echo "ASSET=$zip"
}

echo "Packaging Blink Rest $VERSION for macOS (build $BUILD_NUMBER)"
build_arch arm64
build_arch x86_64
