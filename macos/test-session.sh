#!/bin/sh
# Dev only: types through 海酷拼音 in a test window on this Mac and prints
# what arrived (macos/test-session.swift). Install it first (macos/install.sh).
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
out="$PWD/build/test-session.txt"
rm -f "$out"
open -W -n "$app" --args "$out"
cat "$out"
