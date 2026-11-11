#!/bin/sh
# Checks that installed packages put things where IBus and Fcitx5 look:
# for a fresh container after installing them (linux/arch.sh, linux/rpm.sh;
# a .deb the same way). Checks whichever front ends are installed.
#   linux/check-install.sh
set -e
failed=0
ok() { echo "ok   $*"; }
fail() { echo "FAIL $*"; failed=1; }
data=/usr/share/haiku-pinyin
[ -s $data/pinyin.dict ] && [ -s $data/bigram.bin ] && [ -f $data/icon.svg ] \
	&& ok "data in $data" || fail "no data in $data"

# IBus reads components from /usr/share/ibus/component; the <exec> there
# must run.
xml=/usr/share/ibus/component/haiku-pinyin.xml
if [ -f $xml ]; then
	exec=$(sed -n 's|.*<exec>\([^ <]*\).*|\1|p' $xml)
	icon=$(sed -n 's|.*<icon>\([^<]*\)</icon>.*|\1|p' $xml)
	ok "IBus component $xml"
	"$exec" --version >/dev/null && ok "IBus engine runs: $exec" \
		|| fail "IBus engine doesn't run: $exec"
	[ -f "$icon" ] && ok "IBus icon $icon" || fail "no IBus icon $icon"
	# The settings window: ibus-setup opens <setup>, GNOME Settings the
	# desktop file named for the engine.
	setup=$(sed -n 's|.*<setup>\([^<]*\)</setup>.*|\1|p' $xml)
	desktop=/usr/share/applications/ibus-setup-haiku-pinyin.desktop
	[ -n "$setup" ] && "$setup" --version >/dev/null \
		&& ok "settings window runs: $setup" || fail "no settings window: '$setup'"
	grep -qx "Exec=$setup" $desktop 2>/dev/null \
		&& ok "GNOME Settings opens it: $desktop" || fail "no $desktop for it"
fi

# Fcitx5 loads addons from the directory its own are in.
if [ -d /usr/share/fcitx5 ] && [ -f /usr/share/fcitx5/addon/haiku-pinyin.conf ]; then
	addons=$(dirname "$(find /usr/lib* -name libclassicui.so -path '*fcitx5*' | head -1)")
	grep -q '^Library=libhaiku-pinyin$' /usr/share/fcitx5/addon/haiku-pinyin.conf \
		&& ok "Fcitx5 addon /usr/share/fcitx5/addon/haiku-pinyin.conf" \
		|| fail "Fcitx5 addon conf names another library"
	[ -f /usr/share/fcitx5/inputmethod/haiku-pinyin.conf ] \
		&& ok "Fcitx5 input method /usr/share/fcitx5/inputmethod/haiku-pinyin.conf" \
		|| fail "no Fcitx5 input method conf"
	if [ -f "$addons/libhaiku-pinyin.so" ]; then
		ok "Fcitx5 addon library beside Fcitx5's own: $addons/libhaiku-pinyin.so"
		missing=$(ldd "$addons/libhaiku-pinyin.so" | grep 'not found' || true)
		[ -z "$missing" ] && ok "its libraries are all there" || fail "missing: $missing"
	else
		fail "no libhaiku-pinyin.so in $addons"
	fi
	[ -f /usr/share/icons/hicolor/scalable/apps/haiku-pinyin.svg ] \
		&& ok "icon in the hicolor theme" || fail "no hicolor icon"
fi
exit $failed
