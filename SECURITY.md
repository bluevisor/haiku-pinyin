# Security

An input method sees every key you press, so a security problem here
matters more than in most small projects. Thank you for reporting one.

## Reporting a vulnerability

Report it privately on GitHub:
**[Security → Report a vulnerability](https://github.com/bluevisor/haiku-pinyin/security/advisories/new)**.
Only the maintainer sees it. Please don't open a public issue, pull request
or discussion for it.

Say what's affected (platform, version, which part), how to reproduce it,
and what an attacker gets. A proof of concept helps but isn't required.

This is a small project run by one person. You'll get a reply within a
week, usually sooner; a confirmed problem gets a fix and a release as soon
as it can, and an advisory that credits you unless you'd rather not be
named.

Things that matter most here:

- anything that could send typed text, learned words or the text around
  the cursor off the machine (see [PRIVACY.md](PRIVACY.md) for what's meant
  to leave, and when)
- the Mac's self-update (`macos/Updater.swift`): getting it to install
  something that isn't a signed release
- the AI model download (`macos/Models.swift`) and the cloud request
  (`linux/Cloud.cpp`, `haiku/PinyinMethod.cpp`, `macos/InputController.mm`)
- the release signing: the packages, the apt repository, the checksums

## Supported versions

Only the **latest release** gets security fixes. 海酷拼音 is at 0.1.x and
moves quickly; an update is the fix.

| Version | Supported |
|---|---|
| latest 0.1.x release | yes |
| anything older | no: update |
| your own build | build the latest `main` |

The Mac release updates itself once a day; on Debian and Ubuntu,
`sudo apt update && sudo apt upgrade` brings the fix.

## How releases are signed

- **All platforms**: `SHA256SUMS` in each release, signed with the
  project's release key in `SHA256SUMS.asc`. The key is
  [`tools/release-key.asc`](tools/release-key.asc), fingerprint
  `1016 60C1 F375 2B00 10E7  25F6 E34D C770 E462 7F58`. How to check a
  download: [Checking a download](README.md#checking-a-download).
- **macOS**: the app is signed with a Developer ID and notarized by Apple,
  and the `.pkg` with a Developer ID Installer certificate. The updater
  takes a new version only when its SHA-256 matches `latest.json` and it's
  signed by the same Developer ID team.
- **Linux**: the apt repository's `InRelease` is signed with the release
  key, and apt refuses packages that aren't.
- **Haiku**: an `.hpkg` can't carry a signature; check it against the
  signed `SHA256SUMS`.

The signing keys stay on the maintainer's machine; CI builds and tests but
never signs.
