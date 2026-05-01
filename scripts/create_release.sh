#!/bin/bash
# Usage: scripts/create_release.sh <tag>
#   e.g. scripts/create_release.sh v1.6.3-docmodel.7
#
# Workflow:
#   1. Derive the expected debian/changelog version from the tag.
#   2. Verify the top changelog entry matches — abort if not.
#   3. Commit the changelog if it is the only staged/unstaged change.
#   4. Create + push the tag, then create the GitHub release.

set -euo pipefail

TAG="${1:-}"
if [[ -z "$TAG" ]]; then
    echo "Usage: $0 <tag>  (e.g. v1.6.3-docmodel.7)" >&2
    exit 1
fi

# Derive expected changelog version: v1.6.3-docmodel.7 -> 1.6.3+docmodel7
CHANGELOG_VERSION=$(echo "$TAG" | sed 's/^v//' | sed 's/-docmodel\./+docmodel/')

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$ROOT_DIR"

# Check top entry in debian/changelog
TOP_VERSION=$(head -1 debian/changelog | grep -oP '\(\K[^)]+' | sed 's/-[0-9]*$//')

if [[ "$TOP_VERSION" != "$CHANGELOG_VERSION" ]]; then
    echo "ERROR: debian/changelog top entry is '$TOP_VERSION' but expected '$CHANGELOG_VERSION'." >&2
    echo "Update debian/changelog for $CHANGELOG_VERSION before releasing." >&2
    exit 1
fi

echo "changelog OK: $CHANGELOG_VERSION"

# Commit the changelog if it has uncommitted changes
if ! git diff --quiet debian/changelog || ! git diff --cached --quiet debian/changelog; then
    git add debian/changelog
    git commit -m "debian/changelog: bump to ${TAG#v}"
    echo "Committed changelog."
fi

# Tag
if git rev-parse "$TAG" >/dev/null 2>&1; then
    echo "Tag $TAG already exists — skipping tag creation."
else
    git tag -a "$TAG" -m "$TAG"
    echo "Created tag $TAG."
fi

git push origin "$TAG"
echo "Pushed tag $TAG."

# Extract release notes from changelog (lines between first and second "^cherrytree" headers)
NOTES=$(awk '/^cherrytree/{n++; if(n==2) exit} n==1 && !/^cherrytree/' debian/changelog \
    | sed '/^[[:space:]]*--/d' \
    | sed 's/^  \* /- /' \
    | sed '/^$/d')

gh release create "$TAG" \
    --repo martinakos/cherrytree \
    --title "$TAG" \
    --target "$(git rev-parse HEAD)" \
    --notes "Changes since previous release:

$NOTES"

echo "Release $TAG created."
