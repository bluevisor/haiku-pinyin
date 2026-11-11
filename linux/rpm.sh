#!/bin/sh
# Fedora packages, made with rpmbuild in Docker from the source tarball of
# tag v<version> and the data asset of that release, as Copr would:
#   linux/rpm.sh             dist/fedora/: the spec, the source rpm and the
#                            rpms (haiku-pinyin-data, ibus-haiku-pinyin,
#                            fcitx5-haiku-pinyin), from this tree
#                            (linux/sources.sh) before the tag exists
#   linux/rpm.sh --release   the same from the published tag and asset
# Then installs the rpms in a fresh container and checks where the files land
# (linux/check-install.sh). Built for this machine's architecture.
set -e
cd "$(dirname "$0")/.."
version=$(cat VERSION)
out=dist/fedora
rm -rf "$out" && mkdir -p "$out"
source=haiku-pinyin-$version.tar.gz
data=haiku-pinyin-data-$version.tar.xz
if [ "$1" = --release ]; then
	url=https://github.com/bluevisor/haiku-pinyin
	curl -fsSL -o "$out/$source" "$url/archive/v$version/$source"
	curl -fsSL -o "$out/$data" "$url/releases/download/v$version/$data"
else
	linux/sources.sh >/dev/null
	cp "dist/$source" "dist/$data" "$out/"
fi
sed -e "s|@VERSION@|$version|g" -e "s|@DATE@|$(LC_ALL=C date -u '+%a %b %d %Y')|" \
	linux/fedora/haiku-pinyin.spec.in > "$out/haiku-pinyin.spec"

docker run --rm -v "$PWD/$out":/out fedora:latest sh -ec '
	dnf -q -y install rpm-build gcc-c++ make "pkgconfig(ibus-1.0)" "pkgconfig(gtk+-3.0)" \
		"pkgconfig(Fcitx5Core)" "pkgconfig(Fcitx5Config)" "pkgconfig(Fcitx5Utils)" \
		>/dev/null
	top=/root/rpmbuild
	mkdir -p $top/SOURCES $top/SPECS
	cp /out/*.tar.* $top/SOURCES/ && cp /out/haiku-pinyin.spec $top/SPECS/
	rpmbuild -ba $top/SPECS/haiku-pinyin.spec >/tmp/rpmbuild.log 2>&1 \
		|| { tail -30 /tmp/rpmbuild.log; exit 1; }
	grep -E "^(all passed|Wrote:)" /tmp/rpmbuild.log
	cp $top/SRPMS/*.rpm $top/RPMS/*/*.rpm /out/'

echo ">> installing them in a fresh container"
docker run --rm -v "$PWD/$out":/out:ro \
	-v "$PWD/linux/check-install.sh":/check-install.sh:ro fedora:latest sh -ec '
	dnf -q -y install $(ls /out/*.rpm | grep -v -e "\.src\.rpm$" -e "-debug") >/dev/null
	rpm -q haiku-pinyin-data ibus-haiku-pinyin fcitx5-haiku-pinyin
	sh /check-install.sh'
