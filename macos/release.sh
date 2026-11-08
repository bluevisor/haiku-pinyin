#!/bin/sh
# Builds the Mac download: HaikuPinyin.app signed with the Developer ID,
# notarized and stapled, in a zip (what the app's updater fetches) and a
# disk image (what people download), plus latest.json, the updater's feed.
# All in dist/; publish them as the assets of GitHub release v<version>.
#   macos/release.sh
#   SIGN="Developer ID Application: …" NOTARY_PROFILE=<notarytool profile>
#   NOTES="what changed" macos/release.sh
# Notarizing takes a few minutes, twice (the app, then the disk image).
set -e
cd "$(dirname "$0")/.."
version=$(cat VERSION)
SIGN=${SIGN:-"Developer ID Application: Zheng Zheng (CKGXYS3R8N)"}
NOTARY_PROFILE=${NOTARY_PROFILE:-terminal-cam-notary}
NOTES=${NOTES:-$(git log -1 --format=%s)}
app=build/HaikuPinyin.app
zip=dist/HaikuPinyin-$version-mac.zip
dmg=dist/HaikuPinyin-$version.dmg

fail() {
	echo "release: $*" >&2
	exit 1
}

# notarize <file>: submits it and waits; fails unless Apple accepts it.
# `submit --wait` can give up while the submission is still in progress
# (only its id printed, no status): it's waited for again by id.
notarize() {
	log=$(xcrun notarytool submit "$1" --keychain-profile "$NOTARY_PROFILE" \
		--wait 2>&1) || true
	id=$(echo "$log" | sed -n 's/^  id: //p' | head -1)
	if [ -n "$id" ] && ! echo "$log" | grep -q "status: \(Accepted\|Invalid\|Rejected\)"; then
		log=$(xcrun notarytool wait "$id" --keychain-profile "$NOTARY_PROFILE" 2>&1) || true
	fi
	echo "  id: $id"
	echo "$log" | grep -q "status: Accepted" || { echo "$log" >&2; fail "notarization \
of $1 wasn't accepted (xcrun notarytool log $id --keychain-profile $NOTARY_PROFILE)"; }
	echo "  status: Accepted"
}

xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" >/dev/null 2>&1 \
	|| fail "notarytool profile \"$NOTARY_PROFILE\" doesn't work"
[ -f build/data/pinyin.dict ] || make data

echo "Building $version, signed by $SIGN"
SIGN="$SIGN" macos/build.sh >/dev/null
codesign --verify --deep --strict "$app" || fail "the signature doesn't verify"

echo "Notarizing the app"
mkdir -p dist
rm -f build/notarize.zip
ditto -c -k --keepParent "$app" build/notarize.zip
notarize build/notarize.zip
xcrun stapler staple -q "$app"
rm -f "$zip"
ditto -c -k --keepParent "$app" "$zip"

echo "Notarizing the disk image"
stage=build/dmg
rm -rf "$stage" "$dmg"
mkdir -p "$stage"
cp -R "$app" "$stage/"
hdiutil create -quiet -ov -volname "海酷拼音 Haiku Pinyin" -srcfolder "$stage" \
	-format UDZO "$dmg"
codesign --force --timestamp -s "$SIGN" "$dmg"
notarize "$dmg"
xcrun stapler staple -q "$dmg"

sha=$(shasum -a 256 "$zip" | cut -d' ' -f1)
minimum=$(/usr/libexec/PlistBuddy -c "Print LSMinimumSystemVersion" \
	"$app/Contents/Info.plist")
VERSION=$version SHA=$sha MINIMUM=$minimum NOTES=$NOTES python3 - <<'EOF' \
	> dist/latest.json
import json, os
version = os.environ["VERSION"]
print(json.dumps({
	"version": version,
	"url": "https://github.com/bluevisor/haiku-pinyin/releases/download/"
		f"v{version}/HaikuPinyin-{version}-mac.zip",
	"sha256": os.environ["SHA"],
	"notes": os.environ["NOTES"],
	"minimumSystemVersion": os.environ["MINIMUM"],
}, ensure_ascii=False, indent=1))
EOF

# As a browser download would be checked.
check=build/quarantine-check.dmg
cp "$dmg" "$check"
xattr -w com.apple.quarantine "0081;$(printf %x "$(date +%s)");Safari;" "$check"
spctl -a -t open --context context:primary-signature -v "$check" 2>&1 \
	| sed 's/^/  /'
rm -f "$check"
spctl -a -t exec -v "$app" 2>&1 | sed 's/^/  /'

echo "Done: $zip, $dmg, dist/latest.json. Publish all three as the assets of"
echo "GitHub release v$version."
