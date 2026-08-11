#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/publish.sh [--dry-run] VERSION

Examples:
  scripts/publish.sh 1.1.0
  scripts/publish.sh --dry-run 1.1.0

The publish workflow:
  1. validates the repository and GitHub CLI state
  2. runs the BlinkRest unit tests
  3. builds and verifies arm64 and x86_64 Release apps
  4. creates ZIP assets and SHA-256 checksums
  5. pushes main, creates and pushes an annotated version tag
  6. creates the GitHub release with generated release notes
EOF
}

die() {
    echo "publish: $*" >&2
    exit 1
}

DRY_RUN=0
VERSION=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dry-run)
            DRY_RUN=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            [[ -z "$VERSION" ]] || die "only one version may be supplied"
            VERSION="$1"
            ;;
    esac
    shift
done

[[ -n "$VERSION" ]] || {
    usage >&2
    exit 2
}
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "VERSION must use X.Y.Z semantic-version form"

TAG="v$VERSION"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT="BlinkRest.xcodeproj"
SCHEME="BlinkRest"
APP_NAME="BlinkRest.app"
EXECUTABLE_NAME="BlinkRest"
DIST_DIR="$ROOT/dist/$TAG"
TMP_ROOT="${TMPDIR:-/private/tmp}"
WORK_DIR="$(mktemp -d "$TMP_ROOT/blinkrest-release.XXXXXX")"
BUILD_NUMBER="$(git -C "$ROOT" rev-list --count HEAD)"
ARM64_ZIP="$DIST_DIR/BlinkRest-$TAG-macos-arm64.zip"
X86_64_ZIP="$DIST_DIR/BlinkRest-$TAG-macos-x86_64.zip"
CHECKSUMS="$DIST_DIR/SHA256SUMS.txt"

cleanup() {
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

run() {
    if (( DRY_RUN )); then
        printf '+'
        printf ' %q' "$@"
        printf '\n'
    else
        "$@"
    fi
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

for command_name in git gh xcodebuild codesign ditto lipo shasum; do
    require_command "$command_name"
done
[[ -x /usr/libexec/PlistBuddy ]] || die "required command not found: /usr/libexec/PlistBuddy"

cd "$ROOT"
[[ "$(git rev-parse --show-toplevel)" == "$ROOT" ]] || die "run this from the Blink Rest repository"
[[ "$(git branch --show-current)" == "main" ]] || die "publishing is only allowed from the main branch"

if (( ! DRY_RUN )); then
    [[ -z "$(git status --porcelain)" ]] || die "working tree must be clean; commit or stash changes first"
    gh auth status >/dev/null
    git fetch origin main --tags
    git merge-base --is-ancestor origin/main HEAD || die "local main does not contain origin/main; pull or rebase before publishing"
    if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
        die "tag already exists: $TAG"
    fi
    if gh release view "$TAG" >/dev/null 2>&1; then
        die "GitHub release already exists: $TAG"
    fi
    [[ ! -e "$DIST_DIR" ]] || die "release output already exists: $DIST_DIR"
fi

echo "Publishing Blink Rest $VERSION (build $BUILD_NUMBER)"
(( DRY_RUN )) && echo "Dry run: no tests, builds, tags, pushes, or releases will be executed."

run mkdir -p "$DIST_DIR"

run xcodebuild test -quiet \
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

    run xcodebuild -quiet \
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

    run codesign --force --deep --sign - "$app"
    run codesign --verify --deep --strict "$app"

    if (( ! DRY_RUN )); then
        local actual_version
        local actual_archs
        actual_version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")"
        [[ "$actual_version" == "$VERSION" ]] || die "$arch app version is $actual_version, expected $VERSION"
        actual_archs="$(lipo -archs "$app/Contents/MacOS/$EXECUTABLE_NAME")"
        [[ " $actual_archs " == *" $arch "* ]] || die "$arch build has unexpected architecture(s): $actual_archs"
    fi

    run ditto -c -k --sequesterRsrc --keepParent "$app" "$zip"
}

build_arch arm64
build_arch x86_64

if (( DRY_RUN )); then
    echo "+ (cd $DIST_DIR && shasum -a 256 $(basename "$ARM64_ZIP") $(basename "$X86_64_ZIP") > $(basename "$CHECKSUMS"))"
else
    (
        cd "$DIST_DIR"
        shasum -a 256 "$(basename "$ARM64_ZIP")" "$(basename "$X86_64_ZIP")" > "$(basename "$CHECKSUMS")"
    )
fi

run git push origin main
run git tag -a "$TAG" -m "Blink Rest $VERSION"
run git push origin "$TAG"
run gh release create "$TAG" \
    "$ARM64_ZIP" \
    "$X86_64_ZIP" \
    "$CHECKSUMS" \
    --verify-tag \
    --title "Blink Rest $VERSION" \
    --generate-notes

if (( DRY_RUN )); then
    echo "Dry run complete for $TAG."
else
    RELEASE_URL="$(gh release view "$TAG" --json url --jq .url)"
    echo "Published: $RELEASE_URL"
    echo "Assets retained in: $DIST_DIR"
fi
