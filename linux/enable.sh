#!/bin/sh
# Adds 海酷拼音 to GNOME's input sources (after sudo make install) and
# switches to it. Other desktops: pick it in ibus-setup instead.
set -e
ibus write-cache
# GNOME runs ibus-daemon as a systemd user service; "ibus restart" stops
# that one for good (it exits cleanly, so systemd doesn't bring it back).
unit=org.freedesktop.IBus.session.GNOME.service
if systemctl --user is-active -q "$unit" 2>/dev/null; then
	systemctl --user restart "$unit"
else
	ibus restart
fi
sources=$(gsettings get org.gnome.desktop.input-sources sources)
case "$sources" in
*"'haiku-pinyin'"*) ;;
"@a(ss) []") gsettings set org.gnome.desktop.input-sources sources \
	"[('ibus', 'haiku-pinyin')]" ;;
*) gsettings set org.gnome.desktop.input-sources sources \
	"${sources%]}, ('ibus', 'haiku-pinyin')]" ;;
esac
for i in 1 2 3 4 5 6 7 8 9 10; do
	ibus engine haiku-pinyin 2>/dev/null && break
	sleep 1
done
echo "海酷拼音 is on; Super+Space cycles input sources."
