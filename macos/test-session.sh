#!/bin/sh
# Dev only: types through 海酷拼音 in a test window on this Mac and prints
# what arrived (macos/test-session.swift): full pinyin with the default
# settings, then 拼音加加 shuangpin. Install it first (macos/install.sh).
set -e
cd "$(dirname "$0")/.."
app=build/HPSessionTest.app
mkdir -p "$app/Contents/MacOS"
swiftc -O macos/test-session.swift -o "$app/Contents/MacOS/HPSessionTest"
cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>HPSessionTest</string>
<key>CFBundleIdentifier</key><string>com.bluevisor.HPSessionTest</string>
<key>CFBundlePackageType</key><string>APPL</string>
</dict></plist>
PLIST
codesign --force -s - "$app" 2>/dev/null
# Each pass sets the method's settings and restarts it so it reads them, with
# no learned words (what it learns from the test would change your typing).
# Your settings and words are put back afterwards.
domain=com.bluevisor.inputmethod.HaikuPinyin
saved=$PWD/build/test-session-settings.plist
words="$HOME/Library/Application Support/HaikuPinyin/user.dict"
saved_words=$PWD/build/test-session-user.dict
rm -f "$saved" "$saved_words"
defaults export $domain "$saved" 2>/dev/null || true
pkill -x HaikuPinyin || true	# it saves its words as it quits
sleep 0.5
[ -f "$words" ] && mv "$words" "$saved_words"
restore() {
	defaults delete $domain 2>/dev/null || true
	if [ -f "$saved" ]; then defaults import $domain "$saved"; rm -f "$saved"; fi
	pkill -x HaikuPinyin || true
	sleep 0.5
	rm -f "$words"
	[ -f "$saved_words" ] && mv "$saved_words" "$words"
}
trap restore EXIT

failed=0
# pass <name> <expected text> [defaults write arguments…]
pass() {
	name=$1 expected=$2
	shift 2
	defaults delete $domain 2>/dev/null || true
	# The AI's moves depend on the model: only its own pass has it on.
	defaults write $domain aiPicks -bool NO
	defaults write $domain aiComplete -bool NO
	while [ $# -gt 0 ]; do
		defaults write $domain "$1" $2 "$3"
		shift 3
	done
	pkill -x HaikuPinyin || true
	sleep 0.5
	rm -f "$words"
	out="$PWD/build/test-session.txt"
	rm -f "$out"
	open -W -n "$app" --args "$out" "$name"
	got=$(sed '$d' "$out")	# the last line says how it ended
	if [ "$got" = "$expected" ]; then
		echo "PASS $name"
	else
		echo "FAIL $name: $(tail -1 "$out")"
		printf 'got:\n%s\nwant:\n%s\n' "$got" "$expected"
		failed=1
	fi
}

pass full "中国人，hello 你好。bjdx入
我在学习中文生日快乐
谢谢你好
你好。壹拾贰←7"
pass shuangpin "中国，啥玩意儿啊为什么
nihd输入法中国
饿我壹拾贰" layout -string pyjj vowels -int 2
# Needs the models (macos/ai/fetch-models.sh); skipped without them.
if [ -f "$HOME/Library/Application Support/HaikuPinyin/models/Qwen3-1.7B-Base-4bit/config.json" ]; then
	pass ai "我妹妹说她
祝你生日快乐
中华人民北京大，" aiPicks -bool YES aiComplete -bool YES
fi
exit $failed
