#!/bin/sh
# Builds the Linux .deb packages in Docker, for a release:
#   linux/deb.sh            -> dist/haiku-pinyin_<v>_arm64.deb and _amd64.deb,
#                              dist/fcitx5-haiku-pinyin_<v>_arm64.deb and _amd64.deb,
#                              dist/haiku-pinyin-data_<v>_all.deb
#   linux/deb.sh amd64      just one architecture (and the data)
# Built on Ubuntu 22.04 (glibc 2.35, Fcitx5 5.0), so they install on Ubuntu
# 22.04+ and Debian 12+; Depends comes from dpkg-shlibdeps. Run `make data`
# first: the data goes in as built here. amd64 runs under emulation on Apple
# silicon.
#
# Three packages, the way Debian splits ibus-rime, fcitx5-rime and rime-data:
#   haiku-pinyin         the IBus engine where IBus looks (/usr), and
#                        haiku-pinyin-enable (linux/enable.sh) for each user
#                        to run once; the name it always had, so apt upgrade
#                        carries people over
#   fcitx5-haiku-pinyin  the Fcitx5 addon, where Fcitx5 looks
#   haiku-pinyin-data    what both need: the data and the icon. It also adds
#                        the project's apt repository (linux/haiku-pinyin.sources)
#                        and the release key (tools/release-key.asc), so apt
#                        upgrade brings new versions of either, checked
#                        against that key; tools/sign-release.sh makes the
#                        repository. It takes these over from the
#                        haiku-pinyin packages from before the split
#                        (Replaces), conffile and all.
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
mkdir -p "$tree/build/data" && cp build/data/* "$tree/build/data/"
# apt wants the key in binary form.
gpg --dearmor < tools/release-key.asc > "$tree/build/archive-keyring.gpg"
data=yes
for arch in $archs; do
	echo ">> haiku-pinyin $version $arch"
	docker run --rm --platform "linux/$arch" -v "$tree":/src:ro \
		-v "$PWD/dist":/out -e VERSION="$version" -e ARCH="$arch" \
		-e DATA="$data" ubuntu:22.04 sh -ec '
		export DEBIAN_FRONTEND=noninteractive
		apt-get update -qq
		apt-get install -qq -y --no-install-recommends build-essential \
			pkg-config libibus-1.0-dev fcitx5-modules-dev libfcitx5core-dev \
			libfcitx5config-dev libfcitx5utils-dev dpkg-dev >/dev/null
		cp -R /src /w && cd /w
		make -j"$(nproc)" ibus fcitx5 >/dev/null
		root=/w/root
		make install-ibus DESTDIR=$root >/dev/null
		make install-fcitx5 DESTDIR=$root >/dev/null
		install -Dm755 linux/enable.sh $root/usr/bin/haiku-pinyin-enable
		install -Dm644 build/archive-keyring.gpg \
			$root/usr/share/keyrings/haiku-pinyin-archive-keyring.gpg
		install -Dm644 linux/haiku-pinyin.sources \
			$root/etc/apt/sources.list.d/haiku-pinyin.sources
		addon=$(cd $root && echo usr/lib/*/fcitx5/libhaiku-pinyin.so)
		strip $root/usr/libexec/ibus-engine-haiku-pinyin $root/$addon
		mkdir -p debian
		printf "Source: haiku-pinyin\n\nPackage: haiku-pinyin\nArchitecture: any\n" > debian/control

		# package <name> <architecture> <paths...>: the paths, out of $root
		package() {
			name=$1 architecture=$2
			shift 2
			pkg=/w/pkg/$name
			for path; do
				mkdir -p $pkg/$(dirname $path)
				cp -R $root/$path $pkg/$path
			done
			install -Dm644 LICENSE $pkg/usr/share/doc/$name/copyright
			mkdir -p $pkg/DEBIAN
			size=$(du -sk $pkg | cut -f1)
			{
				echo "Package: $name"
				echo "Version: $VERSION"
				echo "Architecture: $architecture"
				echo "Maintainer: John Zheng <bluevisor@gmail.com>"
				echo "Installed-Size: $size"
				cat
				echo "Section: utils"
				echo "Priority: optional"
				echo "Homepage: https://github.com/bluevisor/haiku-pinyin"
			} > $pkg/DEBIAN/control
		}
		build() {
			dpkg-deb --root-owner-group -Zxz --build /w/pkg/$1 \
				/out/$1_${VERSION}_$2.deb >/dev/null
			echo "dist/$1_${VERSION}_$2.deb"
		}
		depends() {
			deps=$(dpkg-shlibdeps -O -e$1 | sed -n "s/^shlibs:Depends=//p")
			[ -n "$deps" ] || { echo "dpkg-shlibdeps found no dependencies" >&2; exit 1; }
			echo "$deps"
		}
		features="Full pinyin and shuangpin (拼音加加, 小鹤, 自然码, 微软, 智能ABC), whole
 sentences scored with word pairs, abbreviations, 模糊音, mixed Chinese and
 English, emoji, v-mode (numbers in 大写, dates, symbols), 简体 and 繁體,
 and words it learns from you."

		if [ "$DATA" = yes ]; then
			package haiku-pinyin-data all usr/share/haiku-pinyin usr/share/icons \
				usr/share/keyrings etc/apt <<EOF
Breaks: haiku-pinyin (<< $VERSION)
Replaces: haiku-pinyin (<< $VERSION)
Description: 海酷拼音 Haiku Pinyin, a Chinese input method: its data
 The dictionaries, word pairs, shuangpin layouts and 简/繁 tables that the
 IBus engine (haiku-pinyin) and the Fcitx5 addon (fcitx5-haiku-pinyin) share.
 Adds the project apt repository, signed with the release key, so apt
 upgrade brings new versions.
EOF
			echo /etc/apt/sources.list.d/haiku-pinyin.sources \
				> /w/pkg/haiku-pinyin-data/DEBIAN/conffiles
			build haiku-pinyin-data all
		fi

		deps=$(depends $root/usr/libexec/ibus-engine-haiku-pinyin)
		package haiku-pinyin $ARCH usr/libexec usr/share/ibus usr/bin <<EOF
Depends: ibus, haiku-pinyin-data (= $VERSION), $deps
Recommends: curl, ca-certificates
Description: 海酷拼音 Haiku Pinyin, a Chinese input method for IBus
 $features
 After installing, run haiku-pinyin-enable once as yourself (GNOME), or
 add it in ibus-setup. For Fcitx5, install fcitx5-haiku-pinyin instead.
EOF
		cat > /w/pkg/haiku-pinyin/DEBIAN/postinst <<EOF
#!/bin/sh
set -e
command -v ibus >/dev/null && ibus write-cache --system 2>/dev/null || true
EOF
		chmod 755 /w/pkg/haiku-pinyin/DEBIAN/postinst
		build haiku-pinyin $ARCH

		deps=$(depends $root/$addon)
		package fcitx5-haiku-pinyin $ARCH $(dirname $addon) usr/share/fcitx5 <<EOF
Depends: fcitx5, haiku-pinyin-data (= $VERSION), $deps
Recommends: curl, ca-certificates
Description: 海酷拼音 Haiku Pinyin, a Chinese input method for Fcitx5
 $features
 After installing, restart Fcitx5 and add 海酷拼音 Haiku Pinyin in
 fcitx5-configtool. For IBus, install haiku-pinyin instead.
EOF
		build fcitx5-haiku-pinyin $ARCH'
	data=no
done
