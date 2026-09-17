#!/usr/bin/env bash
# Builds the Haiku package (.hpkg) on Haiku. Run from the repository root.
set -euo pipefail
TC_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$TC_ROOT"
NAME=$(awk '$1 == "name" { print $2; exit }' resources/TurboChook.PackageInfo)
VERSION=$(awk '$1 == "version" { print $2; exit }' resources/TurboChook.PackageInfo)
ARCH=$(awk '$1 == "architecture" { print $2; exit }' resources/TurboChook.PackageInfo)
FILE="$TC_ROOT/artifacts/$NAME-$VERSION-$ARCH.hpkg"
make -j4
STAGE=$(mktemp -d /tmp/turbochook-package-XXXXXX)
trap 'rm -rf -- "$STAGE"' EXIT
mkdir -p "$STAGE/apps" "$STAGE/documentation/packages/turbochook/vendor/nlohmann" \
    "$STAGE/data/deskbar/menu/Applications" "$STAGE/boot/post-install" "$TC_ROOT/artifacts"
cp build-haiku/TurboChook "$STAGE/apps/TurboChook"
strip --strip-debug "$STAGE/apps/TurboChook"
# GNU strip removes the appended Haiku resources; restore them.
xres -o "$STAGE/apps/TurboChook" build-haiku/TurboChook.rsrc
cp resources/TurboChook.PackageInfo "$STAGE/.PackageInfo"
cp README.md LICENSE "$STAGE/documentation/packages/turbochook/"
cp -R docs "$STAGE/documentation/packages/turbochook/"
cp vendor/nlohmann/LICENSE.MIT vendor/nlohmann/UPSTREAM.md "$STAGE/documentation/packages/turbochook/vendor/nlohmann/"
cp resources/turbochook-post-install.sh "$STAGE/boot/post-install/turbochook.sh"
ln -s ../../../../apps/TurboChook "$STAGE/data/deskbar/menu/Applications/TurboChook"
( cd "$STAGE" && mimeset --all -f --mimedb data/mime_db --mimedb /boot/system/data/mime_db apps/TurboChook )
catattr -r BEOS:ICON "$STAGE/apps/TurboChook" > "$STAGE/.icon-check"
cmp resources/branding/turbochook-icon.hvif "$STAGE/.icon-check"
rm "$STAGE/.icon-check"
package create -C "$STAGE" "$FILE"
printf '%s\n' "$FILE"
