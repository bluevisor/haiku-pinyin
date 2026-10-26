#!/bin/sh
# Installs the add-on and its data into ~/config/non-packaged. Run on Haiku
# after make. Files the input_server has loaded or mapped are replaced by
# renaming a finished copy over them, never rewritten in place: changing a
# mapped add-on crashes the input_server (no keyboard, no mouse).
set -e
cd "$(dirname "$0")/.."

replace() {	# file, directory; the copy waits outside watched folders
	staging=$(dirname "$2")/.haiku-pinyin-$(basename "$1").new
	cp "$1" "$staging"
	mv "$staging" "$2/$(basename "$1")"
}

data=$(finddir B_USER_NONPACKAGED_DATA_DIRECTORY)/haiku-pinyin
mkdir -p "$data"
for file in build/data/*; do
	replace "$file" "$data"
done

methods=$(finddir B_USER_NONPACKAGED_ADDONS_DIRECTORY)/input_server/methods
mkdir -p "$methods"
replace build/HaikuPinyin "$methods"
echo "Installed. Pick 海酷拼音 from the keyboard menu in Deskbar, or press"
echo "Alt+Space (Cmd+Space with Kigo's remap) to cycle input methods."
