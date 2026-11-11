#!/bin/sh
# Builds .deb packages of the IBus engine in Docker, for a release:
#   linux/deb.sh                 -> dist/haiku-pinyin_<v>_arm64.deb and _amd64.deb
#   linux/deb.sh amd64           just one
# Built on Ubuntu 22.04 (glibc 2.35), so they install on Ubuntu 22.04+ and
# Debian 12+; Depends comes from dpkg-shlibdeps. Run `make data` first: the
# data goes in as built here. amd64 runs under emulation on Apple silicon.
#
# The package puts the engine where IBus looks (/usr), and
# haiku-pinyin-enable (linux/enable.sh) on the PATH for each user to run once.
# It also adds the project's apt repository (linux/haiku-pinyin.sources) and
# the release key (tools/release-key.asc), so apt upgrade brings new versions,
# checked against that key; tools/sign-release.sh makes the repository.
set -e
cd "$(dirname "$0")/.."
[ -f build/data/pinyin.dict ] || { echo "no build/data: run make data" >&2; exit 1; }
version=$(cat VERSION)
archs=${*:-arm64 amd64}
mkdir -p dist
tree=$(mktemp -d)
trap 'rm -rf "$tree"' EXIT
# The tracked files and the built data, nothing of the Mac's own builds.
git archive HEAD | tar xf - -C "$tree"
mkdir -p "$tree/build" && cp -R build/data "$tree/build/"
# apt wants the key in binary form.
gpg --dearmor < tools/release-key.asc > "$tree/build/archive-keyring.gpg"
for arch in $archs; do
	echo ">> haiku-pinyin $version $arch"
	docker run --rm --platform "linux/$arch" -v "$tree":/src:ro \
		-v "$PWD/dist":/out -e VERSION="$version" -e ARCH="$arch" \
		ubuntu:22.04 sh -ec '
		export DEBIAN_FRONTEND=noninteractive
		apt-get update -qq
		apt-get install -qq -y --no-install-recommends build-essential \
			pkg-config libibus-1.0-dev dpkg-dev >/dev/null
		cp -R /src /w && cd /w
		make -j"$(nproc)" ibus >/dev/null
		pkg=/w/pkg
		make install-ibus DESTDIR=$pkg >/dev/null
		install -Dm755 linux/enable.sh $pkg/usr/bin/haiku-pinyin-enable
		install -Dm644 LICENSE $pkg/usr/share/doc/haiku-pinyin/copyright
		install -Dm644 build/archive-keyring.gpg \
			$pkg/usr/share/keyrings/haiku-pinyin-archive-keyring.gpg
		install -Dm644 linux/haiku-pinyin.sources \
			$pkg/etc/apt/sources.list.d/haiku-pinyin.sources
		strip $pkg/usr/libexec/ibus-engine-haiku-pinyin
		mkdir -p debian $pkg/DEBIAN
		printf "Source: haiku-pinyin\n\nPackage: haiku-pinyin\nArchitecture: any\n" > debian/control
		deps=$(dpkg-shlibdeps -O -e$pkg/usr/libexec/ibus-engine-haiku-pinyin \
			| sed -n "s/^shlibs:Depends=//p")
		[ -n "$deps" ] || { echo "dpkg-shlibdeps found no dependencies" >&2; exit 1; }
		size=$(du -sk $pkg/usr | cut -f1)
		cat > $pkg/DEBIAN/control <<EOF
Package: haiku-pinyin
Version: $VERSION
Architecture: $ARCH
Maintainer: John Zheng <bluevisor@gmail.com>
Installed-Size: $size
Depends: ibus, $deps
Recommends: curl, ca-certificates, zenity, libnotify-bin
Section: utils
Priority: optional
Homepage: https://github.com/bluevisor/haiku-pinyin
Description: 海酷拼音 Haiku Pinyin, a Chinese input method for IBus
 Full pinyin and shuangpin (拼音加加, 小鹤, 自然码, 微软, 智能ABC), whole
 sentences scored with word pairs, abbreviations, 模糊音, mixed Chinese and
 English, emoji, v-mode (numbers in 大写, dates, symbols), 简体 and 繁體,
 and words it learns from you. After installing, run haiku-pinyin-enable
 once as yourself (GNOME), or add it in ibus-setup. Adds its apt
 repository, signed with the release key, so apt upgrade brings new versions.
EOF
		cat > $pkg/DEBIAN/postinst <<EOF
#!/bin/sh
set -e
command -v ibus >/dev/null && ibus write-cache --system 2>/dev/null || true
EOF
		chmod 755 $pkg/DEBIAN/postinst
		echo /etc/apt/sources.list.d/haiku-pinyin.sources > $pkg/DEBIAN/conffiles
		dpkg-deb --root-owner-group -Zxz --build $pkg \
			/out/haiku-pinyin_${VERSION}_${ARCH}.deb >/dev/null
		echo "dist/haiku-pinyin_${VERSION}_${ARCH}.deb"'
done
