#!/bin/sh
# Installs the IBus engine into ~/.local, no root needed. IBus only reads
# components from /usr/share/ibus/component unless IBUS_COMPONENT_PATH says
# otherwise, so this sets it for the systemd user session (GNOME starts
# ibus-daemon from a systemd user unit) and restarts IBus.
#   linux/install-user.sh            install
#   linux/install-user.sh --remove   uninstall
# With root, `sudo make install-ibus` puts everything under /usr instead.
set -e
cd "$(dirname "$0")/.."

prefix=$HOME/.local
data=${XDG_DATA_HOME:-$HOME/.local/share}
components=$data/ibus/component
envfile=${XDG_CONFIG_HOME:-$HOME/.config}/environment.d/haiku-pinyin.conf
path=/usr/share/ibus/component:$components

restart_ibus() {
	rm -f "${XDG_CACHE_HOME:-$HOME/.cache}"/ibus/bus/registry*
	for unit in org.freedesktop.IBus.session.GNOME.service \
			org.freedesktop.IBus.session.generic.service; do
		if systemctl --user -q is-active "$unit" 2>/dev/null; then
			systemctl --user restart "$unit"
			return
		fi
	done
	IBUS_COMPONENT_PATH=$path ibus-daemon -drx 2>/dev/null || true
}

if [ "$1" = --remove ]; then
	make uninstall-ibus PREFIX="$prefix" DATADIR="$data/haiku-pinyin" \
		COMPONENTDIR="$components"
	rm -f "$envfile"
	systemctl --user unset-environment IBUS_COMPONENT_PATH 2>/dev/null || true
	restart_ibus
	echo "Removed. Your words and settings stay in ~/.config/haiku-pinyin."
	exit 0
fi

make install-ibus PREFIX="$prefix" DATADIR="$data/haiku-pinyin" \
	COMPONENTDIR="$components"

mkdir -p "$(dirname "$envfile")"
echo "IBUS_COMPONENT_PATH=$path" > "$envfile"
systemctl --user set-environment IBUS_COMPONENT_PATH="$path" 2>/dev/null || true
restart_ibus

echo
echo "Add it under Settings > Keyboard > Input Sources > + > Chinese >"
echo "海酷拼音 Haiku Pinyin, or:"
echo "  gsettings set org.gnome.desktop.input-sources sources \\"
echo "    \"[('xkb', 'us'), ('ibus', 'haiku-pinyin')]\""
