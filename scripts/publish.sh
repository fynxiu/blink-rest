#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/publish.sh [--dry-run] [--platforms macos|windows|both] VERSION

Examples:
  scripts/publish.sh 1.2.4
  scripts/publish.sh --platforms windows 1.2.5
  scripts/publish.sh --dry-run --platforms both 1.3.0

Publishing is performed by the GitHub Actions release workflow. This script
validates local state and dispatches that workflow with an explicit platform
selection. The workflow owns builds, the version tag, release assets, and the
combined SHA256SUMS.txt file.
EOF
}

die() {
    echo "publish: $*" >&2
    exit 1
}

DRY_RUN=0
VERSION=""
PLATFORMS="both"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dry-run)
            DRY_RUN=1
            ;;
        --platforms)
            shift
            [[ $# -gt 0 ]] || die "--platforms requires macos, windows, or both"
            PLATFORMS="$1"
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
case "$PLATFORMS" in
    macos|windows|both) ;;
    *) die "--platforms must be macos, windows, or both" ;;
esac

TAG="v$VERSION"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

for command_name in git gh; do
    command -v "$command_name" >/dev/null 2>&1 || die "required command not found: $command_name"
done

cd "$ROOT"
[[ "$(git rev-parse --show-toplevel)" == "$ROOT" ]] || die "run this from the Blink Rest repository"
[[ "$(git branch --show-current)" == "main" ]] || die "publishing is only allowed from the main branch"

if (( DRY_RUN )); then
    echo "Dispatching Blink Rest $VERSION for: $PLATFORMS"
    printf '+ gh workflow run release.yml --ref main -f %q -f %q\n' "version=$VERSION" "platforms=$PLATFORMS"
    echo "Dry run complete for $TAG."
    exit 0
fi

[[ -z "$(git status --porcelain)" ]] || die "working tree must be clean; commit or stash changes first"

gh auth status >/dev/null
git fetch origin main --tags
git merge-base --is-ancestor origin/main HEAD || die "local main does not contain origin/main; pull or rebase before publishing"
[[ "$(git rev-parse HEAD)" == "$(git rev-parse origin/main)" ]] || die "local main must exactly match origin/main before publishing"

if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    die "tag already exists: $TAG"
fi
if gh release view "$TAG" >/dev/null 2>&1; then
    die "GitHub release already exists: $TAG"
fi

echo "Dispatching Blink Rest $VERSION for: $PLATFORMS"
gh workflow run release.yml \
    --ref main \
    -f "version=$VERSION" \
    -f "platforms=$PLATFORMS"

echo "Release workflow dispatched for $TAG."
echo "Track it with: gh run list --workflow release.yml --limit 5"
