#!/bin/sh
# Wraps build/HaikuPinyin.app in an installer package, dist/HaikuPinyin-<v>.pkg.
# It installs for the current user only (no admin password), into
# ~/Library/Input Methods like the app's own installer, then registers and
# turns on the method (macos/pkg/postinstall). macos/release.sh runs it on
# the signed, stapled app and notarizes the result; on its own:
#   macos/pkg.sh                                   unsigned, for testing
#   INSTALLER_SIGN="Developer ID Installer: …" macos/pkg.sh
# Test an install with: installer -pkg dist/HaikuPinyin-<v>.pkg -target CurrentUserHomeDirectory
set -e
cd "$(dirname "$0")/.."
version=$(cat VERSION)
app=build/HaikuPinyin.app
out=dist/HaikuPinyin-$version.pkg
[ -d "$app" ] || { echo "pkg: no $app: run macos/build.sh" >&2; exit 1; }

work=build/pkg
rm -rf "$work"
mkdir -p "$work/root" "$work/scripts" dist
ditto "$app" "$work/root/HaikuPinyin.app"
install -m 755 macos/pkg/postinstall "$work/scripts/postinstall"

# Installer otherwise "relocates" the bundle to wherever it finds another copy
# with the same identifier (a build in a work tree, say) and updates that.
pkgbuild --analyze --root "$work/root" "$work/component.plist" >/dev/null
/usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" \
	-c "Set :0:BundleIsVersionChecked false" "$work/component.plist"
pkgbuild --quiet --root "$work/root" --component-plist "$work/component.plist" \
	--scripts "$work/scripts" --install-location "/Library/Input Methods" \
	--identifier com.bluevisor.inputmethod.HaikuPinyin.pkg --version "$version" \
	"$work/HaikuPinyin.pkg"

sed "s/@VERSION@/$version/g" macos/pkg/Distribution.xml > "$work/Distribution.xml"
rm -f "$out"
if [ -n "$INSTALLER_SIGN" ]; then
	productbuild --quiet --distribution "$work/Distribution.xml" \
		--package-path "$work" --resources macos/pkg/resources \
		--sign "$INSTALLER_SIGN" --timestamp "$out"
else
	productbuild --quiet --distribution "$work/Distribution.xml" \
		--package-path "$work" --resources macos/pkg/resources "$out"
fi
echo "$out"
