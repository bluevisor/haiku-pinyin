// 海酷拼音 Haiku Pinyin for macOS: what the settings window's Copy puts on
// the clipboard for a bug report. The version, this Mac, the settings, the
// AI's state and the end of its log, and the updater's state, in English
// (whoever reads it may not read Chinese). Never what was typed, nor the
// learned words: the AI's log only says what loaded and what failed, and
// HaikuPinyin.log (debugLog: every key) is left out. The home folder shows
// as ~.
import AppKit
import Foundation

// AI.mm.
@_silgen_name("HPAIReady")
func aiReadyNow() -> Bool
@_silgen_name("HPAIHelperProcess")
func aiHelperProcess() -> Int32

private let logLines = 50

func diagnosticsReport() -> String {
	var lines: [String] = []
	func section(_ title: String) {
		lines.append("")
		lines.append("## " + title)
	}
	let bundle = Bundle.main
	let version = bundle.object(forInfoDictionaryKey: "CFBundleShortVersionString")
		as? String ?? "?"
	let updater = Updater.shared
	lines.append("Haiku Pinyin \(version), "
		+ (updater.canUpdate ? "release" : "development build")
		+ ", at \(bundle.bundlePath)")
	lines.append("macOS \(ProcessInfo.processInfo.operatingSystemVersionString)")
	lines.append("\(sysctlString("machdep.cpu.brand_string") ?? "?")"
		+ " (\(sysctlString("hw.model") ?? "?")), "
		+ ByteCountFormatter.string(fromByteCount:
			Int64(ProcessInfo.processInfo.physicalMemory), countStyle: .memory)
		+ " memory")
	lines.append("Languages: \(Locale.preferredLanguages.joined(separator: ", "))"
		+ "; the window's: \(uiLanguage)")

	section("Settings")
	let defaults = UserDefaults.standard
	let saved = defaults.persistentDomain(forName: bundle.bundleIdentifier
		?? ProcessInfo.processInfo.processName) ?? [:]
	let registered = defaults.volatileDomain(forName: UserDefaults.registrationDomain)
	let names = Set(saved.keys).union(registered.keys)
		.filter { !$0.hasPrefix("NS") && !$0.hasPrefix("Apple") && !$0.hasPrefix("com.apple") }
	for name in names.sorted() {
		let value = saved[name] ?? registered[name]!
		lines.append("\(name) = \(describe(value))" + (saved[name] == nil ? " (default)" : ""))
	}

	section("AI")
	lines.append("supported: \(aiSupported ? "yes" : "no (needs Apple silicon and the helper)")")
	lines.append("models folder: \(modelsFolder().path)")
	let models = installedModels()
	lines.append("installed: " + (models.isEmpty ? "none"
		: models.map { "\($0.name) (\($0.size))" }.joined(separator: ", ")))
	let choice = defaults.string(forKey: "aiModel") ?? ""
	lines.append("chosen: " + (choice.isEmpty ? "automatic" : choice)
		+ (choice.isEmpty || models.contains { $0.name == choice } ? "" : " (not there)"))
	let process = aiHelperProcess()
	lines.append("usable: \(aiUsableNow() ? "yes" : "no"), failed to load: "
		+ "\(aiFailedNow() ? "yes" : "no"), ready: \(aiReadyNow() ? "yes" : "no"), "
		+ "helper: " + (process != 0 ? "running (\(process))" : "not running"))
	let downloader = ModelDownloader.shared
	lines.append("download: " + (downloader.running
		? "running, \(downloader.done) of \(downloader.total) bytes"
		: "idle") + (downloader.message.isEmpty ? "" : ", \"\(downloader.message)\""))
	lines.append("invitation shown: \(defaults.bool(forKey: "aiInvited") ? "yes" : "no")")

	section("Updates")
	lines.append("automatic: \(updater.automatic ? "yes" : "no"), "
		+ "busy: \(updater.busy ? "yes" : "no")")
	lines.append("last check: " + (updater.lastCheck.map {
		ISO8601DateFormatter().string(from: $0) } ?? "never"))
	lines.append("status: " + (updater.status.isEmpty ? "-" : updater.status))
	lines.append("waiting to install: " + (updater.staged ?? "nothing"))
	if let feed = defaults.string(forKey: "updateFeed") {
		lines.append("feed: \(feed)")
	}

	let log = NSHomeDirectory() + "/Library/Logs/HaikuPinyinAI.log"
	section("The last \(logLines) lines of ~/Library/Logs/HaikuPinyinAI.log")
	if let text = try? String(contentsOfFile: log, encoding: .utf8) {
		let tail = text.split(separator: "\n", omittingEmptySubsequences: false)
			.suffix(logLines + 1).joined(separator: "\n")
			.trimmingCharacters(in: .newlines)
		lines.append(tail.isEmpty ? "(empty)" : tail)
	} else {
		lines.append("(none)")
	}
	return lines.joined(separator: "\n")
		.replacingOccurrences(of: NSHomeDirectory(), with: "~") + "\n"
}

func copyDiagnostics() {
	let board = NSPasteboard.general
	board.clearContents()
	board.setString(diagnosticsReport(), forType: .string)
}

private func sysctlString(_ name: String) -> String? {
	var size = 0
	guard sysctlbyname(name, nil, &size, nil, 0) == 0, size > 0 else { return nil }
	var bytes = [CChar](repeating: 0, count: size)
	guard sysctlbyname(name, &bytes, &size, nil, 0) == 0 else { return nil }
	return String(cString: bytes)
}

private func describe(_ value: Any) -> String {
	switch value {
	case let date as Date: return ISO8601DateFormatter().string(from: date)
	case let string as String: return "\"\(string)\""
	default: return "\(value)"
	}
}
