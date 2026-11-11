#!/bin/sh
# The two tarballs the Arch and Fedora packages build from, in dist/:
#   haiku-pinyin-data-<v>.tar.xz  build/data, for the release: the packages
#                                 can't fetch and build it themselves
#   haiku-pinyin-<v>.tar.gz       the tree at HEAD, laid out as GitHub's
#                                 tarball of tag v<v>, to build before the
#                                 tag exists
# Run `make data` first.
set -e
cd "$(dirname "$0")/.."
[ -f build/data/pinyin.dict ] || { echo "no build/data: run make data" >&2; exit 1; }
version=$(cat VERSION)
mkdir -p dist
staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT
mkdir "$staging/haiku-pinyin-data-$version"
cp build/data/* "$staging/haiku-pinyin-data-$version/"
# No macOS extended attributes or ._ files in it.
COPYFILE_DISABLE=1 tar -C "$staging" --no-xattrs -cJf \
	"dist/haiku-pinyin-data-$version.tar.xz" "haiku-pinyin-data-$version"
git archive --format=tar.gz --prefix="haiku-pinyin-$version/" \
	-o "dist/haiku-pinyin-$version.tar.gz" HEAD
echo "dist/haiku-pinyin-data-$version.tar.xz"
echo "dist/haiku-pinyin-$version.tar.gz"
