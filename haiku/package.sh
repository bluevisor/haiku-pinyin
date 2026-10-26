#!/bin/bash
# Wraps the add-on and its data in an .hpkg. Run ON Haiku, after
# `make data` (or with build/data copied over):
#   haiku/package.sh                     -> dist/haiku_pinyin-<v>-<rev>-<arch>.hpkg
#   HP_PKG_REV=2 haiku/package.sh        bump the package revision
#   HP_ADDON=build/x86_64/HaikuPinyin HP_ARCH=x86_64 haiku/package.sh
#                                        package a cross-built add-on
# Install by copying the .hpkg into ~/config/packages, then run
# haiku_pinyin_restart (or log out and in) so the input_server loads it.
set -eo pipefail
cd "$(dirname "$0")/.."

version=$(cat VERSION)
rev=${HP_PKG_REV:-1}
arch=${HP_ARCH:-$(getarch)}
out="dist/haiku_pinyin-$version-$rev-$arch.hpkg"
addon=${HP_ADDON:-build/HaikuPinyin}
[ -n "$HP_ADDON" ] || make -j4 build/HaikuPinyin >/dev/null

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/add-ons/input_server/methods" "$stage/data/haiku-pinyin" \
	"$stage/bin" "$stage/documentation/packages/haiku_pinyin"
install -m 755 "$addon" "$stage/add-ons/input_server/methods/HaikuPinyin"
cp build/data/* "$stage/data/haiku-pinyin/"
install -m 755 haiku/restart-input-server.sh "$stage/bin/haiku_pinyin_restart"
cp README.md LICENSE "$stage/documentation/packages/haiku_pinyin/"
sed -e "s/@VERSION@/$version/g" -e "s/@REVISION@/$rev/g" -e "s/@ARCH@/$arch/g" \
	haiku/PackageInfo.in > "$stage/.PackageInfo"

mkdir -p dist
rm -f "$out"
package create -C "$stage" "$out"
echo "$out"
