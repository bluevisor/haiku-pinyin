#!/bin/sh
# Dev only: types through 海酷拼音 into gedit on this machine's GNOME/X11
# session and prints what arrived. Run over ssh with the session's env:
#   DISPLAY=:0 XAUTHORITY=... DBUS_SESSION_BUS_ADDRESS=... linux/test-session.sh
# Needs xdotool; 海酷拼音 must be the active input source.
set -e
export GTK_IM_MODULE=ibus XMODIFIERS=@im=ibus
file=/tmp/hp-session-test.txt
rm -f "$file"
pkill -x gedit || true
# Wait for the old window to go, or the search below finds it.
while xdotool search --name hp-session-test >/dev/null 2>&1; do sleep 0.2; done
(setsid gedit --new-window "$file" >/dev/null 2>&1 &)
w=$(xdotool search --sync --onlyvisible --name hp-session-test | head -1)
xdotool windowactivate --sync "$w" 2>/dev/null || true
for i in 1 2 3 4 5 6 7 8 9 10; do
	[ "$(xdotool getactivewindow 2>/dev/null)" = "$w" ] && break
	sleep 0.3
done
sleep 0.3
[ "$(ibus engine)" = haiku-pinyin ] || { echo "active engine: $(ibus engine)"; exit 1; }

# Every keystroke goes to whatever has focus: stop the moment that isn't
# gedit (a password prompt, a notification), rather than type into it.
focused() {
	[ "$(xdotool getactivewindow 2>/dev/null)" = "$w" ] && return
	echo "gedit lost focus; stopped" >&2
	exit 1
}
t() { focused; xdotool type --delay 60 "$1"; sleep 0.3; }
k() { focused; xdotool key --delay 120 "$@"; sleep 0.3; }
shift_tap() { focused; xdotool keydown Shift_L; sleep 0.05; xdotool keyup Shift_L; sleep 0.3; }

t zgr; k space				# 中国人 (abbreviation)
t ,						# ，
shift_tap; t "hello "; shift_tap	# English mode
t nihao; k 1				# 你好, then 联想 shows
t .						# 。 (联想 dropped)
t bjdx; k Return			# bjdx, as typed
t zhongguo; k Escape		# nothing
t shuru; k bracketright		# 入 (以词定字)
k Return
t wozaixuexizhongwen; k space	# a sentence
t shengri; k space; k 1		# 生日, then 联想 1 (快乐)
k Return
t xiexie; k equal minus 1	# page on and back, then 谢谢
t nihaoma; k BackSpace BackSpace space	# 你好
k Return
t "nihao1."					# 你好。 (period after a digit pick)
k ctrl+s
sleep 0.5
cat "$file"; echo
