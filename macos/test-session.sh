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
# Each pass sets the method's settings and restarts it so it reads them;
# yours are put back afterwards.
domain=com.bluevisor.inputmethod.HaikuPinyin
saved=$PWD/build/test-session-settings.plist
rm -f "$saved"
defaults export $domain "$saved" 2>/dev/null || true
restore() {
	defaults delete $domain 2>/dev/null || true
	if [ -f "$saved" ]; then defaults import $domain "$saved"; rm -f "$saved"; fi
	pkill -x HaikuPinyin || true
}
trap restore EXIT

failed=0
# pass <name> <expected text> [defaults write arguments…]
pass() {
	name=$1 expected=$2
	shift 2
	defaults delete $domain 2>/dev/null || true
	while [ $# -gt 0 ]; do
		defaults write $domain "$1" $2 "$3"
		shift 3
	done
	pkill -x HaikuPinyin || true
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
你好。"
pass shuangpin "中国，啥玩意儿啊为什么
nihd输入法中国
饿我" layout -string pyjj vowels -int 2
exit $failed
