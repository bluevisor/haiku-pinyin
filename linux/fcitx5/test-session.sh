#!/bin/sh
# Dev only: types through 海酷拼音 under Fcitx5 into a GTK text view, on a
# virtual X server, and checks what arrived. For a container or VM with the
# addon installed (docker-test.sh sets one up), never a desktop in use: it
# starts its own Xvfb, D-Bus session and fcitx5, and replaces the Fcitx5
# profile and the haiku-pinyin settings of the user running it.
#   linux/fcitx5/test-session.sh [screenshot directory]
# Needs Xvfb, xdotool, dbus, fcitx5 with its GTK 3 module, python3-gi, and
# ImageMagick for the screenshots.
set -e
[ -n "$DBUS_SESSION_BUS_ADDRESS" ] || exec dbus-run-session -- "$0" "$@"
here=$(cd "$(dirname "$0")" && pwd)
shots=${1:-/tmp/hp-fcitx5-shots}
mkdir -p "$shots"
config=${XDG_CONFIG_HOME:-$HOME/.config}
file=/tmp/hp-fcitx5-test.txt
failed=0

export DISPLAY=:99 GTK_IM_MODULE=fcitx QT_IM_MODULE=fcitx XMODIFIERS=@im=fcitx \
	LANG=C.UTF-8
if [ -z "$XDG_RUNTIME_DIR" ]; then
	export XDG_RUNTIME_DIR=/tmp/hp-runtime-$(id -u)
	mkdir -p -m 700 "$XDG_RUNTIME_DIR"
fi
Xvfb :99 -screen 0 1024x600x24 >/tmp/hp-xvfb.log 2>&1 &
for i in $(seq 50); do xdpyinfo >/dev/null 2>&1 && break; sleep 0.1; done

# Every setting written out, with the layout to type in ($1: full, or a
# shuangpin layout such as xiaohe), never left to a default; an input method
# group of the keyboard, then haiku-pinyin ($2: "alone" leaves the keyboard
# out).
setup() {
	rm -rf "$config/haiku-pinyin" "$config/fcitx5"
	mkdir -p "$config/haiku-pinyin" "$config/fcitx5"
	printf '%s\n' "layout=$1" script=0 page_size=7 fullwidth=1 emoji=1 \
		english=1 cloud=0 fuzzy=0 vowels=0 chinese=1 \
		> "$config/haiku-pinyin/settings"
	{
		echo "[Groups/0]"; echo "Name=Default"; echo "Default Layout=us"
		echo "DefaultIM=haiku-pinyin"; echo
		n=0
		if [ "$2" != alone ]; then
			echo "[Groups/0/Items/0]"; echo "Name=keyboard-us"; echo "Layout="; echo
			n=1
		fi
		echo "[Groups/0/Items/$n]"; echo "Name=haiku-pinyin"; echo "Layout="; echo
		echo "[GroupOrder]"; echo "0=Default"
	} > "$config/fcitx5/profile"
}

start() {
	fcitx5 -d --replace >/tmp/hp-fcitx5.log 2>&1
	# Not fcitx5-remote: before fcitx5 has its name, D-Bus would start another.
	for i in $(seq 100); do
		gdbus call --session --dest org.freedesktop.DBus \
			--object-path /org/freedesktop/DBus \
			--method org.freedesktop.DBus.NameHasOwner org.fcitx.Fcitx5 \
			| grep -q true && break
		sleep 0.1
	done
	rm -f "$file" "$file.preedit"
	python3 "$here/textview.py" "$file" &
	app=$!
	w=$(xdotool search --sync --onlyvisible --name hp-fcitx5-test | head -1)
	xdotool windowfocus --sync "$w"
	xdotool mousemove --window "$w" 20 20
	sleep 1
	[ "$1" = alone ] || fcitx5-remote -s haiku-pinyin
	sleep 0.3
	[ "$(fcitx5-remote -n)" = haiku-pinyin ] \
		|| { echo "active input method: $(fcitx5-remote -n)"; exit 1; }
}

stop() {
	kill "$app" 2>/dev/null || true
	wait "$app" 2>/dev/null || true
	fcitx5-remote -e 2>/dev/null || true
	for i in $(seq 50); do pgrep -x fcitx5 >/dev/null || break; sleep 0.1; done
}

t() { xdotool type --delay 60 "$1"; sleep 0.3; }
k() { xdotool key --delay 120 "$@"; sleep 0.3; }
shift_tap() { xdotool keydown "${1:-Shift_L}"; sleep 0.05; xdotool keyup "${1:-Shift_L}"; sleep 0.3; }
shot() { import -window root "$shots/$1.png" 2>/dev/null || true; }
check() {
	got=$(cat "$2" 2>/dev/null || true)
	if [ "$got" = "$3" ]; then
		echo "ok   $1: $got"
	else
		echo "FAIL $1: got '$got', wanted '$3'"
		failed=1
	fi
}
config_set() {	# Fcitx5's configuration tool does the same over D-Bus
	gdbus call --session --dest org.fcitx.Fcitx5 --object-path /controller \
		--method org.fcitx.Fcitx.Controller1.SetConfig \
		fcitx://config/inputmethod/haiku-pinyin "<@a{sv} {$1}>" >/dev/null
	sleep 0.3
}

echo "== 全拼, keyboard first in the group"
setup full
start
t nihao; shot candidates
check "inline composition" "$file.preedit" "ni'hao"
k Escape
t zgr; k space				# 中国人 (abbreviation)
t ,						# ，
shift_tap; t "hello "; shift_tap	# English: Fcitx5 switches to the keyboard
shift_tap Shift_R					# English: 海酷拼音's own
[ "$(fcitx5-remote -n)" = haiku-pinyin ] && echo "ok   Right Shift stays in 海酷拼音" \
	|| { echo "FAIL Right Shift switched to $(fcitx5-remote -n)"; failed=1; }
t "world "; shift_tap Shift_R
t nihao; k 1				# 你好
t .						# 。
t bjdx; k Return			# bjdx, as typed
t zhongguo; k Escape		# nothing
t shuru; k bracketright		# 入 (以词定字)
k Return
t wozaixuexizhongwen; k space	# a sentence
t shengrikuaile; k space	# 生日快乐
k Return
t xiexie; k equal; shot page2; k minus 1	# page on and back, then 谢谢
t nihaoma; k BackSpace BackSpace space	# 你好
k Return
t "nihao1."					# 你好。 (period after a digit pick)
check "全拼" "$file" "中国人，hello world 你好。bjdx入
我在学习中文生日快乐
谢谢你好
你好。"
stop

echo "== 双拼 · 小鹤, from the settings file"
setup xiaohe
start
gdbus call --session --dest org.fcitx.Fcitx5 --object-path /controller \
	--method org.fcitx.Fcitx.Controller1.GetConfig \
	fcitx://config/inputmethod/haiku-pinyin | grep -q "'Layout': <'双拼 · 小鹤'>" \
	&& echo "ok   the settings page shows 小鹤" \
	|| { echo "FAIL the settings page doesn't show 小鹤"; failed=1; }
t nihc; shot xiaohe; k space		# 你好: ni hao
t wozdxtxivswf; k space		# 我在学习中文
t vsgo; k 1					# 中国: zhong guo
t nihao; k space			# 小鹤 reads ni ha o: 尼哈哦
check "小鹤" "$file" "你好我在学习中文中国尼哈哦"
stop

echo "== 中/英 and settings, haiku-pinyin alone in the group"
setup full alone
start alone
shift_tap; t "hello "; shift_tap Shift_R; t nihao; k space	# its own 中/英
check "Shift tap" "$file" "hello 你好"
grep -q '^chinese=1$' "$config/haiku-pinyin/settings" && echo "ok   中/英 saved" \
	|| { echo "FAIL 中/英 not saved"; failed=1; }
gdbus call --session --dest org.fcitx.Fcitx5 --object-path /controller \
	--method org.fcitx.Fcitx.Controller1.GetConfig \
	fcitx://config/inputmethod/haiku-pinyin > /tmp/hp-fcitx5-config.txt
grep -q "双拼 a o e" /tmp/hp-fcitx5-config.txt && echo "ok   the settings page lists the options" \
	|| { echo "FAIL no settings page"; failed=1; }
grep -q "('Cloud', 'Boolean', '云候选 (Google)', <'False'>" /tmp/hp-fcitx5-config.txt \
	&& echo "ok   cloud off by default" || { echo "FAIL cloud on by default"; failed=1; }
config_set "'Script': <'繁體中文'>"
k Return; t xuexi; k space					# 學習
config_set "'Script': <'简体中文'>, 'Layout': <'双拼 · 小鹤'>"
k Return; t nihc; k space					# 你好 in 小鹤
config_set "'Layout': <'全拼'>, 'PageSize': <'5'>"
check "settings" "$file" "hello 你好
學習
你好"
grep -q '^page_size=5$' "$config/haiku-pinyin/settings" \
	&& grep -q '^layout=full$' "$config/haiku-pinyin/settings" \
	&& echo "ok   settings saved to ~/.config/haiku-pinyin/settings" \
	|| { echo "FAIL settings not saved"; cat "$config/haiku-pinyin/settings"; failed=1; }

# Learning: the fifth candidate (十), then the fifth again (时, as 十 moved
# up), and then 时 comes first.
k Return; t shi; shot learn-before; k 5; t shi; k 5
t shi; shot learn-after; k space
stop
check "learning" "$file" "hello 你好
學習
你好
十时时"
grep -q "	时	shi$" "$config/haiku-pinyin/user.dict" \
	&& echo "ok   learned words saved to ~/.config/haiku-pinyin/user.dict" \
	|| { echo "FAIL nothing learned"; failed=1; }

echo "== cloud: off, then turned on"
setup full alone
start alone
t yuanshenqidong; sleep 2.5; shot cloud-off; k Escape	# off by default
config_set "'Cloud': <'True'>"
# Google differs from the local answer (原神启动), so its answer comes second.
cloud=$(curl -s -m 3 "https://inputtools.google.com/request?text=yuanshenqidong&itc=zh-t-i0-pinyin&num=1&cp=0&cs=1&ie=utf-8&oe=utf-8" \
	| sed -n 's/^\["SUCCESS",\[\["yuanshenqidong",\["\([^"]*\)".*/\1/p')
t yuanshenqidong; sleep 2.5; shot cloud; k 2
stop
grep -q '^cloud=1$' "$config/haiku-pinyin/settings" && echo "ok   cloud on" \
	|| { echo "FAIL cloud not saved"; failed=1; }
if [ -n "$cloud" ] && [ "$cloud" != 原神启动 ]; then
	check "cloud" "$file" "$cloud"
else
	echo "skip cloud: no answer from Google, or the same as the local one"
fi

echo "screenshots in $shots"
exit $failed
