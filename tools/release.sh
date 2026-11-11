#!/bin/sh
# The whole release of VERSION, from this Mac:
#   tools/release.sh             (make release) the dry run: checks that a
#                                release can start and lists what it would do
#   tools/release.sh --publish   (make release PUBLISH=1) builds, checks and
#                                publishes it
#   tools/release.sh --check [<version>]
#                                only the final check, of what's in dist/
#                                (DIST=<dir> for another)
# --publish runs, stopping at the first failure:
#   make mac-release     zip, dmg and pkg, signed and notarized; latest.json
#   make linux-deb       IBus, Fcitx5 and data .debs, arm64 and amd64
#   make linux-sources   the data tarball the Arch and Fedora packages fetch
#   the .hpkgs           arm64 built on the Haiku VM (HAIKU_HOST, default
#                        haiku-dbg), x86_64 by haiku/cross.sh; skipped when
#                        both are in dist/ already
#   make sign-release    the apt repository and SHA256SUMS.asc
# then checks every asset and signature, creates GitHub release v<version>
# with the notes from CHANGELOG.md (tools/release-notes.sh) and the downloads
# line, uploads all of it, and renames CHANGELOG's Unreleased to the version
# (a pull request, merged: main takes no direct pushes). The apt repository
# is releases/latest/download/, so a release missing an index file breaks
# apt update for everyone: that's why the asset list is checked here.
set -e
cd "$(dirname "$0")/.."
version=$(cat VERSION)
repo=bluevisor/haiku-pinyin
DIST=${DIST:-dist}
HAIKU_HOST=${HAIKU_HOST:-haiku-dbg}
SIGN=${SIGN:-"Developer ID Application: Zheng Zheng (CKGXYS3R8N)"}
INSTALLER_SIGN=${INSTALLER_SIGN:-"Developer ID Installer: Zheng Zheng (CKGXYS3R8N)"}
NOTARY_PROFILE=${NOTARY_PROFILE:-terminal-cam-notary}
RELEASE_KEY=${RELEASE_KEY:-101660C1F3752B0010E725F6E34DC770E4627F58}
export SIGN INSTALLER_SIGN NOTARY_PROFILE RELEASE_KEY

mode=dry-run
case $1 in
	""|--dry-run) ;;
	--publish) mode=publish ;;
	--check) mode=check; version=${2:-$version} ;;
	*) echo "usage: tools/release.sh [--dry-run | --publish | --check]" >&2; exit 2 ;;
esac
# The builds write dist/; DIST is for checking another.
[ $mode = check ] || [ "$DIST" = dist ] || { echo "release: DIST is for --check only" >&2; exit 2; }

failed=0
ok() { echo "  ok    $*"; }
bad() { echo "  FAIL  $*"; failed=1; }
die() { echo "release: $*" >&2; exit 1; }
# gh as bluevisor: other sessions switch the active account at any time.
gh_() { GH_TOKEN=$(gh auth token --user bluevisor) gh "$@"; }
git_() {
	GH_TOKEN=$(gh auth token --user bluevisor) git -c credential.https://github.com.helper= \
		-c 'credential.https://github.com.helper=!gh auth git-credential' "$@"
}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# The assets of v<version>, all in dist/.
mac_assets="HaikuPinyin-$version-mac.zip HaikuPinyin-$version.dmg HaikuPinyin-$version.pkg latest.json"
debs=
for arch in arm64 amd64; do
	debs="$debs haiku-pinyin_${version}_$arch.deb fcitx5-haiku-pinyin_${version}_$arch.deb"
done
debs="$debs haiku-pinyin-data_${version}_all.deb"
hpkg() { ls "$DIST"/haiku_pinyin-"$version"-*-"$1".hpkg 2>/dev/null | head -1; }
index="Packages Packages.gz Release InRelease Release.gpg"
signed="SHA256SUMS SHA256SUMS.asc haiku-pinyin-release.asc haiku-pinyin-archive-keyring.gpg"
tarball=haiku-pinyin-data-$version.tar.xz
assets() {
	echo $mac_assets $debs $tarball
	for arch in arm64 x86_64; do
		h=$(hpkg $arch)
		if [ -n "$h" ]; then basename "$h"; else echo "haiku_pinyin-$version-1-$arch.hpkg"; fi
	done
	echo $index $signed
}

preflight() {
	echo "Pre-flight for $version"
	branch=$(git rev-parse --abbrev-ref HEAD)
	[ "$branch" = main ] && ok "on main" || bad "on $branch, not main"
	dirty=$(git status --porcelain)
	[ -z "$dirty" ] && ok "clean tree" \
		|| bad "uncommitted changes: $(echo "$dirty" | head -3 | tr '\n' ' ')"
	if git fetch -q origin main 2>"$tmp/fetch"; then
		[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] \
			&& ok "up to date with origin/main" \
			|| bad "HEAD $(git rev-parse --short HEAD) isn't origin/main ($(git rev-parse --short origin/main)): pull or push"
	else
		bad "git fetch origin: $(cat "$tmp/fetch")"
	fi
	if tools/release-notes.sh "$version" > "$tmp/notes" 2>"$tmp/notes.err"; then
		ok "CHANGELOG.md has notes for $version ($(grep -c '^- ' "$tmp/notes") items)"
	else
		bad "$(cat "$tmp/notes.err")"
	fi
	if out=$(gh_ release view "v$version" --repo $repo 2>&1); then
		bad "GitHub release v$version exists already"
	elif echo "$out" | grep -q "release not found"; then
		ok "no GitHub release v$version yet"
	else
		bad "can't ask GitHub about v$version: $out"
	fi
	# The data comes from sources/ as pinned (make data rebuilds it when the
	# pins change).
	(cd sources 2>/dev/null && shasum -a 256 -c "$OLDPWD/tools/sources.sha256" >/dev/null 2>&1) \
		&& ok "sources/ is as tools/sources.sha256 pins it" \
		|| bad "sources/ isn't what tools/sources.sha256 pins: run tools/fetch_sources.sh"
	ids=$(security find-identity -v)
	for cert in "$SIGN" "$INSTALLER_SIGN"; do
		echo "$ids" | grep -qF "\"$cert\"" && ok "certificate $cert" \
			|| bad "no \"$cert\" in the keychain"
	done
	xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" >/dev/null 2>&1 \
		&& ok "notary profile $NOTARY_PROFILE" \
		|| bad "notarytool profile \"$NOTARY_PROFILE\" doesn't work"
	if gpg --list-secret-keys "$RELEASE_KEY" >/dev/null 2>&1; then
		gpg --show-keys --with-colons tools/release-key.asc | grep -q "^fpr:.*$RELEASE_KEY:" \
			&& ok "GPG release key $RELEASE_KEY" \
			|| bad "tools/release-key.asc isn't the key $RELEASE_KEY"
	else
		bad "the release key $RELEASE_KEY isn't in the GnuPG keyring (1Password: \"Haiku Pinyin GPG release key\")"
	fi
	docker info >/dev/null 2>&1 && ok "Docker is running" \
		|| bad "Docker isn't running (open -a Docker)"
	if [ -n "$(hpkg arm64)" ] && [ -n "$(hpkg x86_64)" ]; then
		ok "the .hpkgs are in $DIST: $(basename "$(hpkg arm64)"), $(basename "$(hpkg x86_64)")"
	elif arch=$(ssh -o BatchMode=yes -o ConnectTimeout=5 "$HAIKU_HOST" getarch 2>/dev/null); then
		[ "$arch" = arm64 ] && ok "Haiku VM $HAIKU_HOST is up (arm64)" \
			|| bad "$HAIKU_HOST is $arch, not arm64"
	else
		bad "Haiku VM $HAIKU_HOST doesn't answer, and the .hpkgs for $version aren't in $DIST (start it: open -a UTM-dbg, then start the VM)"
	fi
	gh auth token --user bluevisor >/dev/null 2>&1 && ok "gh can act as bluevisor" \
		|| bad "gh has no bluevisor account (gh auth login)"
}

# The final check: every asset there, for this version, and signed.
check() {
	echo "Checking the assets for $version in $DIST"
	for f in $(assets); do
		[ -s "$DIST/$f" ] && ok "$f" || bad "no $DIST/$f"
	done
	[ $failed = 0 ] || return 0
	d=$DIST
	grep -q "\"version\": \"$version\"" "$d/latest.json" \
		&& grep -q "/v$version/HaikuPinyin-$version-mac.zip" "$d/latest.json" \
		&& grep -q "\"sha256\": \"$(shasum -a 256 "$d/HaikuPinyin-$version-mac.zip" | cut -d' ' -f1)\"" "$d/latest.json" \
		&& ok "latest.json: $version, the zip's URL and checksum" \
		|| bad "latest.json isn't for this version's zip"
	listed=$(grep -c '^Filename: ' "$d/Packages" || true)
	for f in $debs; do
		[ "$(head -c 21 "$d/$f")" = "$(printf '!<arch>\ndebian-binary')" ] \
			&& grep -q "^Filename: $f\$" "$d/Packages" \
			&& grep -q "^SHA256: $(shasum -a 256 "$d/$f" | cut -d' ' -f1)\$" "$d/Packages" \
			|| { bad "$f isn't in Packages as it is"; listed=; }
	done
	if [ -n "$listed" ]; then
		[ "$listed" = 5 ] && ok "Packages lists the five .debs, and only those" \
			|| bad "Packages lists $listed .debs, not the five"
	fi
	for f in Packages Packages.gz; do
		grep -q "$(shasum -a 256 "$d/$f" | cut -d' ' -f1)" "$d/Release" \
			|| bad "Release doesn't have $f's checksum"
	done
	for arch in arm64 x86_64; do
		f=$(hpkg $arch)
		[ "$(head -c 4 "$f")" = hpkg ] && ok "$(basename "$f") is a package" \
			|| bad "$(basename "$f") isn't a Haiku package (zeroed?)"
	done
	tar tJf "$d/$tarball" | grep -q "^haiku-pinyin-data-$version/pinyin.dict$" \
		&& ok "$tarball has the data" || bad "$tarball has no pinyin.dict"

	# Signatures, the way users check them: against the published key only.
	cmp -s "$d/haiku-pinyin-release.asc" tools/release-key.asc \
		&& ok "haiku-pinyin-release.asc is tools/release-key.asc" \
		|| bad "haiku-pinyin-release.asc isn't tools/release-key.asc"
	keyring=$tmp/keyring.gpg
	cp "$d/haiku-pinyin-archive-keyring.gpg" "$keyring"
	for sig in "InRelease" "Release.gpg Release" "SHA256SUMS.asc SHA256SUMS"; do
		# shellcheck disable=SC2086
		(cd "$d" && gpgv --status-fd 1 --keyring "$keyring" $sig 2>/dev/null) \
			| grep -q "VALIDSIG $RELEASE_KEY" && ok "gpg: ${sig%% *} verifies" \
			|| bad "gpg: ${sig%% *} doesn't verify with the archive keyring"
	done
	(cd "$d" && shasum -a 256 -c SHA256SUMS >/dev/null 2>&1) && ok "SHA256SUMS matches" \
		|| bad "SHA256SUMS doesn't match the files"
	for f in $mac_assets $debs $tarball $(basename "$(hpkg arm64)") $(basename "$(hpkg x86_64)"); do
		[ "$f" = latest.json ] && continue
		grep -q "  $f\$" "$d/SHA256SUMS" || bad "SHA256SUMS has no $f"
	done

	pkgutil --check-signature "$d/HaikuPinyin-$version.pkg" 2>&1 | grep -qF "$INSTALLER_SIGN" \
		&& ok "pkgutil: the .pkg is signed by $INSTALLER_SIGN" || bad "the .pkg isn't signed by $INSTALLER_SIGN"
	spctl -a -t install "$d/HaikuPinyin-$version.pkg" 2>/dev/null \
		&& ok "spctl: the .pkg is accepted" || bad "spctl rejects the .pkg"
	spctl -a -t open --context context:primary-signature "$d/HaikuPinyin-$version.dmg" 2>/dev/null \
		&& ok "spctl: the .dmg is accepted" || bad "spctl rejects the .dmg"
	for f in HaikuPinyin-$version.dmg HaikuPinyin-$version.pkg; do
		xcrun stapler validate -q "$d/$f" >/dev/null 2>&1 \
			&& ok "$f has its notarization ticket" || bad "$f isn't stapled"
	done
	ditto -x -k "$d/HaikuPinyin-$version-mac.zip" "$tmp/zip"
	app=$tmp/zip/HaikuPinyin.app
	shown=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$app/Contents/Info.plist" 2>/dev/null)
	[ "$shown" = "$version" ] && ok "the zip's app is $version" || bad "the zip's app is ${shown:-?}, not $version"
	codesign --verify --deep --strict "$app" 2>/dev/null && spctl -a -t exec "$app" 2>/dev/null \
		&& ok "spctl: the zip's app is accepted" || bad "the zip's app doesn't verify"
	# The same version in the disk image and the package, not a renamed one.
	mkdir "$tmp/dmg" "$tmp/pkg"
	if hdiutil attach -quiet -nobrowse -readonly -mountpoint "$tmp/dmg" "$d/HaikuPinyin-$version.dmg"; then
		shown=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" \
			"$tmp/dmg/HaikuPinyin.app/Contents/Info.plist" 2>/dev/null || true)
		hdiutil detach -quiet "$tmp/dmg"
		[ "$shown" = "$version" ] && ok "the .dmg's app is $version" \
			|| bad "the .dmg's app is ${shown:-?}, not $version"
	else
		bad "the .dmg doesn't mount"
	fi
	pkg=$(cd "$d" && pwd)/HaikuPinyin-$version.pkg
	(cd "$tmp/pkg" && xar -xf "$pkg" 2>/dev/null) \
		&& grep -q "<pkg-info [^>]*version=\"$version\"" "$tmp"/pkg/*.pkg/PackageInfo \
		&& ok "the .pkg installs $version" || bad "the .pkg isn't $version"
}

downloads() {
	v=$version
	echo "Downloads: \`HaikuPinyin-$v.pkg\` or \`.dmg\` (macOS 13+, signed and notarized; installed copies update on their own), \`haiku-pinyin_${v}_arm64.deb\` / \`_amd64.deb\` (IBus) or \`fcitx5-haiku-pinyin_${v}_arm64.deb\` / \`_amd64.deb\` (Fcitx5), each with \`haiku-pinyin-data_${v}_all.deb\` (Ubuntu 22.04+, Debian 12+; or \`sudo apt upgrade\`), \`haiku_pinyin-$v-1-arm64.hpkg\` / \`-x86_64.hpkg\` (Haiku 64-bit). Arch and Fedora packages build from \`$tarball\`. Check any download against \`SHA256SUMS\`, signed in \`SHA256SUMS.asc\` with \`haiku-pinyin-release.asc\`."
}

steps() {
	cat <<EOF
  1. make mac-release        zip, dmg, pkg (notarized, a few minutes each), latest.json
  2. make linux-deb          5 .debs (amd64 under emulation: slow)
  3. make linux-sources      $tarball
EOF
	if [ -n "$(hpkg arm64)" ] && [ -n "$(hpkg x86_64)" ]; then
		echo "  4. the .hpkgs              in $DIST already: kept"
	else
		echo "  4. the .hpkgs              arm64 on $HAIKU_HOST, x86_64 by haiku/cross.sh"
	fi
	cat <<EOF
  5. make sign-release       Packages, Release, InRelease, Release.gpg, SHA256SUMS(.asc)
  6. check every asset and signature (gpgv, pkgutil, spctl, stapler)
  7. gh release create v$version --target $(git rev-parse HEAD)
     with these $(assets | wc -w | tr -d ' ') assets:
EOF
	assets | tr ' ' '\n' | grep . | sed 's/^/       /'
	echo "  8. CHANGELOG.md: \"## Unreleased\" -> \"## $version — $(date +%F)\", merged to main"
}

# The arm64 .hpkg, built and packaged on the VM.
haiku_arm64() {
	src='~/src/haiku-pinyin-release'
	mkdir "$tmp/haiku"
	git archive HEAD | tar xf - -C "$tmp/haiku"
	mkdir -p "$tmp/haiku/build/data" && cp build/data/* "$tmp/haiku/build/data/"
	COPYFILE_DISABLE=1 tar czf - --no-xattrs -C "$tmp/haiku" . \
		| ssh "$HAIKU_HOST" "rm -rf $src && mkdir -p $src && cd $src && tar xzf -"
	pkg=$(ssh "$HAIKU_HOST" "cd $src && haiku/package.sh" | tail -1)
	scp -q "$HAIKU_HOST:$src/$pkg" "$DIST/"
	echo "$DIST/$(basename "$pkg")"
}

publish() {
	sha=$(git rev-parse HEAD)
	step() { echo; echo ">> $*"; }
	step "make mac-release"; make mac-release
	step "make linux-deb"; make linux-deb
	step "make linux-sources"; make linux-sources
	if [ -z "$(hpkg arm64)" ] || [ -z "$(hpkg x86_64)" ]; then
		step "Haiku arm64 on $HAIKU_HOST"; haiku_arm64
		step "Haiku x86_64 (haiku/cross.sh)"; HAIKU_HOST=$HAIKU_HOST haiku/cross.sh
	fi
	step "make sign-release"; make sign-release
	echo
	check
	[ $failed = 0 ] || die "the assets don't check out: nothing published"
	[ "$(git rev-parse HEAD)" = "$sha" ] || die "HEAD moved during the build"

	step "GitHub release v$version"
	{ cat "$tmp/notes"; echo; downloads; } > "$tmp/release.md"
	# shellcheck disable=SC2046
	(cd "$DIST" && gh_ release create "v$version" --repo $repo --target "$sha" \
		--title "v$version" --latest --notes-file "$tmp/release.md" $(assets))
	gh_ release view "v$version" --repo $repo --json assets -q '.assets[].name' \
		| sort > "$tmp/uploaded"
	assets | tr ' ' '\n' | grep . | sort > "$tmp/expected"
	cmp -s "$tmp/uploaded" "$tmp/expected" \
		|| die "v$version's assets aren't the list: $(comm -3 "$tmp/expected" "$tmp/uploaded" | tr '\n' ' ')"
	echo "  all $(wc -l < "$tmp/expected" | tr -d ' ') assets uploaded"

	step "CHANGELOG.md: Unreleased is $version"
	branch=changelog-$version
	git checkout -q -b "$branch"
	sed -i '' "s/^## Unreleased\$/## $version — $(date +%F)/" CHANGELOG.md
	git add CHANGELOG.md
	# The version hook makes this commit the next version, as it should.
	git commit -q -m "CHANGELOG: $version"
	git_ push -q origin "$branch"
	url=$(gh_ pr create --repo $repo --base main --head "$branch" \
		--title "CHANGELOG: $version" --body "Unreleased becomes $version, released as v$version.")
	gh_ pr merge "$url" --repo $repo --merge --delete-branch
	git checkout -q main
	git_ pull -q --ff-only origin main
	git branch -q -D "$branch" 2>/dev/null || true
	echo
	echo "Released: https://github.com/$repo/releases/tag/v$version (CHANGELOG: $url)"
}

case $mode in
check)
	tools/release-notes.sh "$version" > "$tmp/notes" 2>/dev/null || true
	check
	[ $failed = 0 ] && echo "All there." || { echo "release: the assets don't check out" >&2; exit 1; }
	;;
dry-run)
	preflight
	echo
	echo "--publish would:"
	steps
	echo
	echo "Notes for v$version:"
	{ cat "$tmp/notes"; echo; downloads; } | sed 's/^/  | /'
	echo
	if [ $failed = 0 ]; then
		echo "Pre-flight passed. Nothing done: run tools/release.sh --publish (make release PUBLISH=1)."
	else
		echo "release: pre-flight failed (FAIL above)." >&2
		exit 1
	fi
	;;
publish)
	preflight
	[ $failed = 0 ] || die "pre-flight failed (FAIL above): nothing done"
	publish
	;;
esac
