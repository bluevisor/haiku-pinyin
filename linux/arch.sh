#!/bin/sh
# Arch packages, made the way the AUR would make them: makepkg in Docker,
# from the source tarball of tag v<version> and the data asset of that
# release.
#   linux/arch.sh             dist/arch/: PKGBUILD and .SRCINFO, and the
#                             packages built from them, from this tree
#                             (linux/sources.sh) before the tag exists
#   linux/arch.sh --release   the same from the published tag and asset, so
#                             the checksums are the ones the AUR needs
# Then installs the packages in a fresh container and checks where the files
# land (linux/check-install.sh). Arch's image is x86_64 only: on Apple
# silicon it runs under emulation, slowly.
set -e
cd "$(dirname "$0")/.."
version=$(cat VERSION)
out=dist/arch
rm -rf "$out" && mkdir -p "$out"
source=haiku-pinyin-$version.tar.gz
data=haiku-pinyin-data-$version.tar.xz
if [ "$1" = --release ]; then
	url=https://github.com/bluevisor/haiku-pinyin
	curl -fsSL -o "$out/$source" "$url/archive/refs/tags/v$version.tar.gz"
	curl -fsSL -o "$out/$data" "$url/releases/download/v$version/$data"
else
	linux/sources.sh >/dev/null
	cp "dist/$source" "dist/$data" "$out/"
fi
sum() { shasum -a 256 "$1" | cut -d' ' -f1; }
sed -e "s|@VERSION@|$version|" -e "s|@SOURCE_SHA256@|$(sum "$out/$source")|" \
	-e "s|@DATA_SHA256@|$(sum "$out/$data")|" linux/arch/PKGBUILD.in > "$out/PKGBUILD"

# pacman's download sandbox fails under emulation: --disable-sandbox.
pacman='pacman -Syu --noconfirm --needed --disable-sandbox'
docker run --rm --platform linux/amd64 -v "$PWD/$out":/out archlinux:latest sh -ec "
	$pacman base-devel ibus fcitx5 gtk3 pkgconf >/dev/null 2>&1
	useradd -m builder
	cp -R /out /home/builder/pkg && chown -R builder /home/builder/pkg
	cd /home/builder/pkg
	su builder -c 'makepkg --printsrcinfo > .SRCINFO && makepkg -f' >/tmp/makepkg.log 2>&1 \
		|| { tail -30 /tmp/makepkg.log; exit 1; }
	grep -E '^(==> (Finished|Starting check)|all passed)' /tmp/makepkg.log || true
	cp .SRCINFO *.pkg.tar.zst /out/"
ls "$out"

echo ">> installing them in a fresh container"
docker run --rm --platform linux/amd64 -v "$PWD/$out":/out:ro \
	-v "$PWD/linux/check-install.sh":/check-install.sh:ro archlinux:latest sh -ec "
	$pacman >/dev/null 2>&1
	pacman -U --noconfirm --disable-sandbox /out/*.pkg.tar.zst >/dev/null 2>&1
	pacman -Q haiku-pinyin-data ibus-haiku-pinyin fcitx5-haiku-pinyin
	sh /check-install.sh"
