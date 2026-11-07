#!/bin/sh
# Builds build/HaikuPinyin.app: the engine and the InputMethodKit front end,
# universal (arm64 + x86_64), with the data inside. Run `make data` first.
#   SIGN="Developer ID Application: …" macos/build.sh    to sign for release
#                                                       (default: ad hoc)
set -e
cd "$(dirname "$0")/.."
[ -f build/data/pinyin.dict ] || { echo "no build/data: run make data" >&2; exit 1; }
version=$(cat VERSION)
app=build/HaikuPinyin.app
contents=$app/Contents

rm -rf "$app"
mkdir -p "$contents/MacOS" "$contents/Resources"
# The engine and controller with clang, then the Swift UI and the link with
# swiftc, once per architecture.
binaries=
for arch in arm64 x86_64; do
	target=$arch-apple-macos13.0
	objects=build/macos/$arch
	mkdir -p "$objects"
	for source in engine/*.cpp macos/*.mm; do
		object=$objects/$(basename "$source").o
		clang++ -target $target -std=c++17 -O2 -g -fobjc-arc \
			-Wall -Wextra -Wno-unused-parameter -c "$source" -o "$object"
	done
	swiftc -target $target -O -g -parse-as-library \
		macos/Settings.swift macos/Candidates.swift \
		"$objects"/*.o -lc++ \
		-framework Cocoa -framework InputMethodKit -framework Carbon \
		-o "$objects/HaikuPinyin"
	binaries="$binaries $objects/HaikuPinyin"
done
lipo -create $binaries -output "$contents/MacOS/HaikuPinyin"
sed "s/@VERSION@/$version/g" macos/Info.plist.in > "$contents/Info.plist"
cp -R macos/Resources/ "$contents/Resources/"
cp -R build/data "$contents/Resources/data"

if [ -n "$SIGN" ]; then
	codesign --force --options runtime --timestamp -s "$SIGN" "$app"
else
	codesign --force -s - "$app"
fi
echo "$app"
