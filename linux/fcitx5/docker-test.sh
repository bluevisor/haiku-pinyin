#!/bin/sh
# Dev only: builds and installs the Fcitx5 addon in a Docker container, then
# types through it there (test-session.sh). Nothing touches this machine's
# own input method. Run `make data` first.
#   linux/fcitx5/docker-test.sh                 build from this tree, Ubuntu 24.04
#   linux/fcitx5/docker-test.sh dist/x.deb ...  install packages instead
# Screenshots land in build/fcitx5-shots/.
set -e
cd "$(dirname "$0")/../.."
[ -f build/data/pinyin.dict ] || { echo "no build/data: run make data" >&2; exit 1; }
image=haiku-pinyin-fcitx5-test
docker build -q -t "$image" - >/dev/null <<'EOF'
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update -qq && apt-get install -qq -y --no-install-recommends \
	build-essential pkg-config python3 fcitx5 fcitx5-frontend-gtk3 \
	fcitx5-modules-dev libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev \
	xvfb xdotool x11-utils dbus libglib2.0-bin python3-gi gir1.2-gtk-3.0 \
	fonts-noto-cjk imagemagick curl ca-certificates >/dev/null
EOF
mkdir -p build/fcitx5-shots
packages=
for deb; do
	packages="$packages /pkgs/$(basename "$deb")"
done
pkgdir=$(cd "$(dirname "${1:-.}")" && pwd)
docker run --rm -v "$PWD":/src:ro -v "$(cd build/data && pwd -P)":/data:ro \
	-v "$pkgdir":/pkgs:ro -v "$PWD/build/fcitx5-shots":/shots \
	-e PACKAGES="$packages" "$image" sh -ec '
	mkdir /w
	(cd /src && tar cf - --exclude ./sources --exclude ./build --exclude ./dist \
		--exclude ./.git .) | tar xf - -C /w
	mkdir -p /w/build/data && cp /data/* /w/build/data/
	cd /w
	if [ -n "$PACKAGES" ]; then
		apt-get install -qq -y $PACKAGES >/dev/null
	else
		make -j"$(nproc)" fcitx5 >/dev/null
		make install-fcitx5 >/dev/null
	fi
	linux/fcitx5/test-session.sh /shots || { tail -20 /tmp/hp-fcitx5.log; exit 1; }'
