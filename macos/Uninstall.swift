// 海酷拼音 Haiku Pinyin for macOS: Uninstall… in the settings window. The
// method is running while it goes, so it can't delete itself: it switches
// the Mac to an English keyboard layout, takes its input source off the
// enabled list, starts a small shell script in a session of its own (so it
// outlives this process) and quits. The script waits for it to be gone,
// stops the AI helper, and deletes the app, and, only when asked, the
// learned words, custom phrases, models, settings and logs.
//   HP_UNINSTALL=keep|all (in the environment the method starts with) runs
//   it as it starts, for testing; HP_UNINSTALL_DRY_RUN=1 makes the script
//   say what it would delete and delete nothing. The script logs to
//   $TMPDIR/HaikuPinyin-uninstall.log.
import AppKit
import Carbon
import SwiftUI

let receiptID = "com.bluevisor.inputmethod.HaikuPinyin.pkg"

// What goes, where.
struct UninstallPlan {
	var app: URL			// …/Input Methods/HaikuPinyin.app
	var updates: URL		// staged updates: a copy of the app, so always
	var support: URL		// ~/Library/Application Support/HaikuPinyin
	var models: URL			// modelsFolder(): in support, or $HAIKU_PINYIN_MODELS
	var logs: [URL]
	var domain: String		// the settings
	var home: URL
	var removeData: Bool
	var dryRun = ProcessInfo.processInfo.environment["HP_UNINSTALL_DRY_RUN"] != nil
	// What touches the rest of the system; nil in a test against a scratch home.
	var receipt: String? = receiptID
	var processName: String? = "HaikuPinyin"

	static func forHome(_ home: URL, removeData: Bool) -> UninstallPlan {
		let support = home.appendingPathComponent("Library/Application Support/HaikuPinyin")
		let logs = home.appendingPathComponent("Library/Logs")
		return UninstallPlan(
			app: home.appendingPathComponent("Library/Input Methods/HaikuPinyin.app"),
			updates: support.appendingPathComponent("Updates"),
			support: support,
			models: ProcessInfo.processInfo.environment["HAIKU_PINYIN_MODELS"]
				.map { URL(fileURLWithPath: $0) } ?? support.appendingPathComponent("models"),
			logs: [logs.appendingPathComponent("HaikuPinyin.log"),
				logs.appendingPathComponent("HaikuPinyinAI.log")],
			domain: Bundle.main.bundleIdentifier ?? "com.bluevisor.inputmethod.HaikuPinyin",
			home: home, removeData: removeData)
	}

	static func current(removeData: Bool) -> UninstallPlan {
		var plan = forHome(URL(fileURLWithPath: NSHomeDirectory()), removeData: removeData)
		plan.app = Bundle.main.bundleURL
		return plan
	}

	// The paths the script deletes, each checked to be in the home folder
	// (or the models folder named by $HAIKU_PINYIN_MODELS) and not it.
	var paths: [URL] {
		var list = [app, updates]
		if removeData {
			list += [support] + logs
			if !models.path.hasPrefix(support.path + "/") { list.append(models) }
		}
		return list
	}

	func safe(_ url: URL) -> Bool {
		let path = url.standardizedFileURL.path
		let base = home.standardizedFileURL.path
		return url.pathComponents.count > 3 && path != base
			&& !base.hasPrefix(path + "/")
			&& (path.hasPrefix(base + "/") || url == models)
	}

	// Waits for the method (and stops the helper), then deletes.
	func script(waitingFor method: Int32, helper: Int32) -> String {
		func quoted(_ text: String) -> String {
			"'" + text.replacingOccurrences(of: "'", with: "'\\''") + "'"
		}
		var lines = [
			"#!/bin/sh",
			"# Haiku Pinyin's Uninstall… (macos/Uninstall.swift).",
			"exec >>\"${TMPDIR:-/tmp}/HaikuPinyin-uninstall.log\" 2>&1",
			"echo \"$(date): uninstalling\(removeData ? ", with the data" : ""), waiting for \(method)\"",
			"n=0",
			"while kill -0 \(method) 2>/dev/null; do",
			"\tn=$((n + 1))",
			"\t[ $n -gt 300 ] && { echo \"it didn't quit; nothing removed\"; exit 1; }",
			"\tsleep 0.1",
			"done",
		]
		if helper > 0 { lines.append("kill \(helper) 2>/dev/null") }
		lines.append(dryRun
			? "remove() { echo \"would remove $1\"; }"
			: "remove() { rm -rf -- \"$1\" && echo \"removed $1\"; }")
		for url in paths where safe(url) {
			lines.append("remove \(quoted(url.path))")
		}
		let run = dryRun ? "echo would run " : ""
		if removeData {
			lines.append("\(run)defaults delete \(quoted(domain))")
		}
		if let processName {
			// Started again by the system in the moment before it was gone.
			lines.append("\(run)pkill -x -U \(getuid()) \(quoted(processName))")
		}
		if let receipt {
			lines.append("pkgutil --volume \(quoted(home.path)) --pkg-info \(quoted(receipt)) >/dev/null 2>&1"
				+ " && \(run)pkgutil --volume \(quoted(home.path)) --forget \(quoted(receipt))")
			// The input menu forgets it; launchd starts it again.
			lines.append("\(run)killall TextInputMenuAgent")
		}
		lines += ["echo done", "rm -f -- \"$0\"", ""]
		return lines.joined(separator: "\n")
	}

	// Starts the script in a new session: not in this process's group,
	// which may be killed with it.
	func launch(waitingFor method: Int32, helper: Int32) -> Bool {
		let file = URL(fileURLWithPath: NSTemporaryDirectory())
			.appendingPathComponent("HaikuPinyin-uninstall-\(method).sh")
		guard (try? script(waitingFor: method, helper: helper)
			.write(to: file, atomically: true, encoding: .utf8)) != nil else { return false }
		var attributes: posix_spawnattr_t?
		posix_spawnattr_init(&attributes)
		posix_spawnattr_setflags(&attributes, Int16(POSIX_SPAWN_SETSID | POSIX_SPAWN_CLOEXEC_DEFAULT))
		var actions: posix_spawn_file_actions_t?
		posix_spawn_file_actions_init(&actions)
		for descriptor in Int32(0) ... 2 {
			posix_spawn_file_actions_addopen(&actions, descriptor, "/dev/null",
				descriptor == 0 ? O_RDONLY : O_WRONLY, 0)
		}
		var pid: pid_t = 0
		let arguments: [UnsafeMutablePointer<CChar>?] = [strdup("/bin/sh"), strdup(file.path), nil]
		defer { arguments.forEach { free($0) } }
		let status = posix_spawn(&pid, "/bin/sh", &actions, &attributes, arguments, environ)
		posix_spawn_file_actions_destroy(&actions)
		posix_spawnattr_destroy(&attributes)
		return status == 0
	}
}

// #pragma mark - input sources

private func property<T>(_ source: TISInputSource, _ key: CFString) -> T? {
	guard let value = TISGetInputSourceProperty(source, key) else { return nil }
	return Unmanaged<AnyObject>.fromOpaque(value).takeUnretainedValue() as? T
}

private func inputSources(_ filter: [CFString: Any], all: Bool) -> [TISInputSource] {
	guard let list = TISCreateInputSourceList(filter as CFDictionary, all)?
		.takeRetainedValue() else { return [] }
	return (list as NSArray).map { $0 as! TISInputSource }
}

private func ourSources() -> [TISInputSource] {
	guard let identifier = Bundle.main.bundleIdentifier else { return [] }
	return inputSources([kTISPropertyBundleID: identifier], all: true)
}

// The English layout to switch to: the one used under the method (ABC,
// U.S., Dvorak…), or ABC, turned on for this when nothing else is.
func englishSource() -> TISInputSource? {
	if let layout = TISCopyCurrentASCIICapableKeyboardLayoutInputSource()?
			.takeRetainedValue(),
		property(layout, kTISPropertyInputSourceIsEnabled) as Bool? == true {
		return layout
	}
	return inputSources([kTISPropertyInputSourceID: "com.apple.keylayout.ABC"],
		all: true).first
}

func englishSourceName() -> String {
	englishSource().flatMap { property($0, kTISPropertyLocalizedName) as String? }
		?? "ABC"
}

// Off the input menu, after switching away: never left typing in a method
// that's gone. On macOS 26, TISDisableInputSource answers noErr for a
// third-party method yet com.apple.inputsources keeps it until the next
// login; once the app is deleted the system stops listing it, so the input
// menu drops it anyway.
private func leaveInputMenu() {
	if let english = englishSource() {
		if property(english, kTISPropertyInputSourceIsEnabled) as Bool? != true {
			TISEnableInputSource(english)
		}
		let current = TISCopyCurrentKeyboardInputSource()?.takeRetainedValue()
		// 中 or 英: both modes have the method's bundle ID.
		let ours = current.flatMap { property($0, kTISPropertyBundleID) as String? }
			== Bundle.main.bundleIdentifier
		if ours || current == nil { TISSelectInputSource(english) }
	}
	for source in ourSources() { TISDisableInputSource(source) }
}

// Uninstall…: false when this copy isn't installed (a build run elsewhere).
@discardableResult
func uninstall(removeData: Bool) -> Bool {
	let plan = UninstallPlan.current(removeData: removeData)
	guard plan.app.deletingLastPathComponent().lastPathComponent == "Input Methods"
	else { return false }
	if !removeData { saveNow() }
	leaveInputMenu()
	guard plan.launch(waitingFor: getpid(), helper: aiHelperProcess()) else { return false }
	NSLog("Haiku Pinyin: uninstalling%@; quitting", removeData ? ", with the data" : "")
	// A moment for the input menu to take the change.
	DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { exit(0) }
	return true
}

// HP_UNINSTALL=keep|all: as Uninstall… with the box unticked or ticked, as
// the method starts (Updater.swift's HPUpdaterStart calls this).
func uninstallIfAsked() {
	guard let mode = ProcessInfo.processInfo.environment["HP_UNINSTALL"] else { return }
	NSLog("Haiku Pinyin: HP_UNINSTALL=%@", mode)
	uninstall(removeData: mode == "all")
}

// #pragma mark - the size of the data

struct UninstallData {
	var modelsSize: Int64 = 0		// what deleting the models folder frees
	var modelsLinked = false		// links to models elsewhere, which stay

	static func measure() -> UninstallData {
		var data = UninstallData()
		let manager = FileManager.default
		let folder = modelsFolder()
		// The enumerator doesn't follow links: a linked model frees nothing.
		if let items = manager.enumerator(at: folder, includingPropertiesForKeys:
				[.totalFileAllocatedSizeKey, .isSymbolicLinkKey]) {
			for case let item as URL in items {
				let values = try? item.resourceValues(forKeys:
					[.totalFileAllocatedSizeKey, .isSymbolicLinkKey])
				if values?.isSymbolicLink == true { data.modelsLinked = true }
				data.modelsSize += Int64(values?.totalFileAllocatedSize ?? 0)
			}
		}
		return data
	}
}

// #pragma mark - the confirmation

struct UninstallSheet: View {
	var cancel: () -> Void
	@State private var removeData = false
	@State private var failed = false
	private let data = UninstallData.measure()
	private let english = englishSourceName()

	var body: some View {
		VStack(alignment: .leading, spacing: 14) {
			HStack(spacing: 10) {
				Tile(symbol: "trash.fill", color: .red)
				Text(L("Uninstall Haiku Pinyin?"))
					.font(.headline)
			}
			VStack(alignment: .leading, spacing: 6) {
				Text(L("This removes:"))
				bullet(L("Haiku Pinyin, from ~/Library/Input Methods"))
				bullet(L("Its input source, from the input menu. It switches you to %@ first.",
					english))
			}
			VStack(alignment: .leading, spacing: 6) {
				Toggle(L("Also delete my data:"), isOn: $removeData)
				Group {
					bullet(L("Learned words and custom phrases"))
					bullet(modelsLine)
					bullet(L("Settings and logs"))
				}
				.padding(.leading, 20)
				.foregroundStyle(removeData ? .primary : .secondary)
			}
			Text(removeData
				? L("This can't be undone. Export your learned words first if you might want them.")
				: L("Your data stays on this Mac, so installing again picks up where you left off."))
				.font(.callout)
				.foregroundStyle(.secondary)
				.fixedSize(horizontal: false, vertical: true)
			if failed {
				Text(L("Couldn't uninstall: this copy isn't in Input Methods."))
					.font(.callout)
					.foregroundStyle(.red)
			}
			HStack {
				Spacer()
				Button(L("Cancel"), action: cancel)
					.keyboardShortcut(.cancelAction)
				Button(L("Uninstall"), role: .destructive) {
					failed = !uninstall(removeData: removeData)
				}
				.buttonStyle(.borderedProminent)
				.tint(.red)
			}
		}
		.padding(20)
		.frame(width: 420)
	}

	private var modelsLine: String {
		let formatter = ByteCountFormatter()
		formatter.countStyle = .file
		formatter.allowsNonnumericFormatting = false	// 0 bytes, not Zero KB
		let size = formatter.string(fromByteCount: data.modelsSize)
		return data.modelsLinked ? L("AI models (%@; models linked from elsewhere stay)", size)
			: L("AI models (%@)", size)
	}

	private func bullet(_ text: String) -> some View {
		HStack(alignment: .firstTextBaseline, spacing: 6) {
			Text(verbatim: "•")
			Text(text).fixedSize(horizontal: false, vertical: true)
		}
	}
}
