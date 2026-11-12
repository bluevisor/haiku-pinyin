#!/bin/sh
# Installs build/HaikuPinyin.app into ~/Library/Input Methods and registers
# it. --remove turns it off and deletes it.
set -e
cd "$(dirname "$0")/.."
dest="$HOME/Library/Input Methods/HaikuPinyin.app"

if [ "$1" = --remove ]; then
	[ -d "$dest" ] && "$dest/Contents/MacOS/HaikuPinyin" --remove
	pkill -x HaikuPinyin || true
	rm -rf "$dest"
	echo "Removed. Log out and in to clear it from the input menu."
	exit 0
fi

[ -d build/HaikuPinyin.app ] || macos/build.sh
# The system starts the new copy the next time the method is used.
pkill -x HaikuPinyin || true
mkdir -p "$HOME/Library/Input Methods"
rm -rf "$dest"
cp -R build/HaikuPinyin.app "$dest"
status=0
"$dest/Contents/MacOS/HaikuPinyin" --install >/dev/null 2>&1 || status=$?
if [ "$status" = 2 ]; then
	open -n "$dest" --args --guide
	exit 0
fi
echo "Installed. If it's new, add it: System Settings → Keyboard → Input Sources"
echo "→ Edit… → + → Chinese, Simplified → Haiku Pinyin."
