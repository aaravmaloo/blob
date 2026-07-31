#!/bin/bash
set -e

if [ -z "$1" ]; then
    echo "Usage: ./update.sh <version>"
    echo "Example: ./update.sh v0.0.4"
    exit 1
fi

VERSION="${1#v}"  # strip leading 'v' if present
TAG="v$VERSION"

echo "==> Fetching SHA256SUMS for $TAG..."
SUMS=$(curl -fsSL "https://github.com/aaravmaloo/blob/releases/download/$TAG/SHA256SUMS")

if [ -z "$SUMS" ]; then
    echo "ERROR: Could not fetch SHA256SUMS. Does release $TAG exist?"
    exit 1
fi

AMD64=$(echo "$SUMS" | grep 'linux-amd64/dist/blob-linux-amd64' | awk '{print $1}')
ARM64=$(echo "$SUMS" | grep 'linux-arm64/dist/blob-linux-arm64' | awk '{print $1}')

if [ -z "$AMD64" ] || [ -z "$ARM64" ]; then
    echo "ERROR: Could not parse hashes from SHA256SUMS"
    echo "$SUMS"
    exit 1
fi

echo "==> amd64: $AMD64"
echo "==> arm64: $ARM64"

echo "==> Updating PKGBUILD..."

# NOTE: sed -i.bak + rm works on BOTH GNU sed (Linux/Arch container)
# and BSD sed (macOS), so this script is safe locally and in CI.
sed -i.bak \
    -e "s/^pkgver=.*/pkgver=$VERSION/" \
    -e "s/^sha256sums_x86_64=('.*')/sha256sums_x86_64=('$AMD64')/" \
    -e "s/^sha256sums_aarch64=('.*')/sha256sums_aarch64=('$ARM64')/" \
    PKGBUILD
rm -f PKGBUILD.bak

echo "==> Regenerating .SRCINFO..."
makepkg --printsrcinfo > .SRCINFO

echo "==> Committing and pushing..."
git add PKGBUILD .SRCINFO
if git diff --cached --quiet; then
    echo "No AUR changes to publish."
    exit 0
fi

git commit -m "update to v$VERSION"
git push

echo "==> Done! blob-bin $VERSION is live on AUR."
