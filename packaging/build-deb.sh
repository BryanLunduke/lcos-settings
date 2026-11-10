#!/bin/sh
# Build lcos-settings_1.1~dev_<arch>.deb with dpkg-buildpackage.
set -eu

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

mkdir -p "$ROOT/packaging/debs"
dpkg-buildpackage -us -uc -b
cp "$ROOT"/../lcos-settings_*.deb "$ROOT/packaging/debs/"
echo "built $(ls "$ROOT"/packaging/debs/lcos-settings_*.deb)"
