#!/bin/sh
# Builds build/HaikuPinyin.app: the engine and the InputMethodKit front end,
# universal (arm64 + x86_64), with the data inside, and the AI helper
# (macos/ai, arm64). Run `make data` first.
#   SIGN="Developer ID Application: …" macos/build.sh    to sign for release
#                                                       (default: ad hoc)
set -e
cd "$(dirname "$0")/.."
[ -f build/data/pinyin.dict ] || { echo "no build/data: run make data" >&2; exit 1; }
version=${HP_VERSION:-$(cat VERSION)}	# HP_VERSION: another, for testing updates
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
		macos/Settings.swift macos/Candidates.swift macos/Models.swift \
		macos/Updater.swift macos/Strings.swift macos/Invitation.swift \
		macos/Diagnostics.swift macos/Words.swift \
		"$objects"/*.o -lc++ \
		-framework Cocoa -framework InputMethodKit -framework Carbon \
		-framework Security \
		-o "$objects/HaikuPinyin"
	binaries="$binaries $objects/HaikuPinyin"
done
lipo -create $binaries -output "$contents/MacOS/HaikuPinyin"
# The AI helper, Apple silicon only (MLX). NO_AI=1 skips it.
if [ -z "$NO_AI" ]; then
	(cd macos/ai && xcodebuild -scheme HaikuPinyinAI -configuration Release \
		-destination 'platform=macOS,arch=arm64' \
		-derivedDataPath ../../build/ai-derived \
		-skipMacroValidation -skipPackagePluginValidation build) \
		> build/ai-build.log 2>&1 \
		|| { echo "AI helper build failed: build/ai-build.log" >&2; exit 1; }
	products=build/ai-derived/Build/Products/Release
	cp "$products/HaikuPinyinAI" "$contents/MacOS/"
	# Its packages' resource bundles (Bundle.module traps when one's
	# missing), and MLX's GPU kernels, which it's told are here: it can't
	# find them in their bundle from inside an app.
	cp -R "$products"/*.bundle "$contents/Resources/"
	cp "$products/mlx-swift_Cmlx.bundle/Contents/Resources/default.metallib" \
		"$contents/Resources/mlx.metallib"
fi
sed "s/@VERSION@/$version/g" macos/Info.plist.in > "$contents/Info.plist"
# The icon, and the words in English, 简体 and 繁體 (Strings.swift).
python3 macos/check-strings.py >&2 || echo "warning: the string tables don't match the code" >&2
cp -R macos/Resources/ "$contents/Resources/"
cp -R build/data "$contents/Resources/data"

# The helper first: an app's signature covers what's nested in it.
sign() {
	if [ -n "$SIGN" ]; then
		codesign --force --options runtime --timestamp -s "$SIGN" "$1"
	else
		codesign --force -s - "$1"
	fi
}
if [ -f "$contents/MacOS/HaikuPinyinAI" ]; then sign "$contents/MacOS/HaikuPinyinAI"; fi
sign "$app"
echo "$app"
