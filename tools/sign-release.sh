#!/bin/sh
# Signs a release in dist/, after macos/release.sh, linux/deb.sh and the
# Haiku packages (haiku/package.sh, copied back here):
#   tools/sign-release.sh
# Adds, beside the downloads:
#   Packages, Packages.gz, Release, InRelease, Release.gpg
#       the apt repository: a flat one, so it can live in the GitHub release
#       itself, at releases/latest/download/ (linux/haiku-pinyin.sources);
#       InRelease and Release.gpg are signed with the release key
#   SHA256SUMS, SHA256SUMS.asc
#       every download's checksum, signed: how a Haiku package (its format has
#       no signature of its own), or any other download, is checked by hand
#   haiku-pinyin-release.asc, haiku-pinyin-archive-keyring.gpg
#       the public key, armored and in the binary form apt takes
# The release key is in this Mac's GnuPG keyring (a backup in 1Password:
# "Haiku Pinyin GPG release key"); RELEASE_KEY picks another.
# Publish everything in dist/ for this version as the release's assets.
set -e
cd "$(dirname "$0")/.."
version=$(cat VERSION)
RELEASE_KEY=${RELEASE_KEY:-101660C1F3752B0010E725F6E34DC770E4627F58}

fail() {
	echo "sign-release: $*" >&2
	exit 1
}

gpg --list-secret-keys "$RELEASE_KEY" >/dev/null 2>&1 \
	|| fail "the release key $RELEASE_KEY isn't in the GnuPG keyring"
# The key in the repository is the one that signs.
gpg --show-keys --with-colons tools/release-key.asc | grep -q "^fpr:.*$RELEASE_KEY:" \
	|| fail "tools/release-key.asc isn't the key $RELEASE_KEY"

# Exactly this version's downloads: five .debs (IBus and Fcitx5 for arm64 and
# amd64, and their data), the data tarball the Arch and Fedora packages build
# from, two .hpkgs and the Mac's three.
cd dist
set -- haiku-pinyin_"$version"_*.deb fcitx5-haiku-pinyin_"$version"_*.deb \
	haiku-pinyin-data_"$version"_all.deb haiku-pinyin-data-"$version".tar.xz \
	haiku_pinyin-"$version"-*.hpkg \
	HaikuPinyin-"$version"-mac.zip HaikuPinyin-"$version".dmg \
	HaikuPinyin-"$version".pkg latest.json
for file; do
	[ -f "$file" ] || fail "no dist/$file"
done
for name in haiku-pinyin fcitx5-haiku-pinyin; do
	debs=$(ls "$name"_"$version"_*.deb | wc -l)
	[ "$debs" -eq 2 ] || fail "$debs $name .debs for $version in dist/, not 2 (arm64, amd64)"
done
grep -q "\"version\": \"$version\"" latest.json || fail "latest.json isn't for $version"
cd ..

cp tools/release-key.asc dist/haiku-pinyin-release.asc
gpg --dearmor < tools/release-key.asc > dist/haiku-pinyin-archive-keyring.gpg

# The index: apt-ftparchive, from Ubuntu, over just this version's .debs.
echo "Indexing the .debs"
repo=$(mktemp -d)
trap 'rm -rf "$repo"' EXIT
cp dist/haiku-pinyin_"$version"_*.deb dist/fcitx5-haiku-pinyin_"$version"_*.deb \
	dist/haiku-pinyin-data_"$version"_all.deb "$repo/"
docker run --rm -v "$repo":/repo -w /repo ubuntu:22.04 sh -ec '
	export DEBIAN_FRONTEND=noninteractive
	apt-get update -qq && apt-get install -qq -y --no-install-recommends apt-utils >/dev/null
	apt-ftparchive packages . | sed "s,^Filename: \./,Filename: ," > Packages
	gzip -9kn Packages
	apt-ftparchive -o APT::FTPArchive::Release::Origin=bluevisor \
		-o APT::FTPArchive::Release::Label="Haiku Pinyin" \
		-o APT::FTPArchive::Release::Suite=stable \
		-o APT::FTPArchive::Release::Architectures="amd64 arm64" \
		-o APT::FTPArchive::Release::Description="海酷拼音 Haiku Pinyin" \
		release . > /tmp/Release && mv /tmp/Release Release'
for file in Packages Packages.gz Release; do
	cp "$repo/$file" dist/
done
rm -f dist/InRelease dist/Release.gpg
gpg --batch --yes -u "$RELEASE_KEY" --digest-algo SHA512 --clearsign \
	-o dist/InRelease dist/Release
gpg --batch --yes -u "$RELEASE_KEY" --digest-algo SHA512 -abs \
	-o dist/Release.gpg dist/Release

echo "Signing the checksums"
cd dist
shasum -a 256 "$@" haiku-pinyin-archive-keyring.gpg > SHA256SUMS
rm -f SHA256SUMS.asc
gpg --batch --yes -u "$RELEASE_KEY" --digest-algo SHA512 --armor \
	--detach-sign -o SHA256SUMS.asc SHA256SUMS
gpg --verify SHA256SUMS.asc SHA256SUMS 2>&1 | grep -q "Good signature" \
	|| fail "SHA256SUMS.asc doesn't verify"
gpg --verify InRelease 2>&1 | grep -q "Good signature" || fail "InRelease doesn't verify"
shasum -a 256 -c SHA256SUMS >/dev/null || fail "SHA256SUMS doesn't match"
cd ..

echo "Done. Publish these as the assets of GitHub release v$version:"
for file in "$@" Packages Packages.gz Release InRelease Release.gpg \
		SHA256SUMS SHA256SUMS.asc haiku-pinyin-release.asc \
		haiku-pinyin-archive-keyring.gpg; do
	echo "  dist/$file"
done
