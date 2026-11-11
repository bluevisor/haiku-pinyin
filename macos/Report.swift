// 海酷拼音 Haiku Pinyin for macOS: Report a Problem… in the settings window.
// It gathers, on this Mac, the diagnostics (Diagnostics.swift) and a summary
// of the crash reports macOS kept for the method and the AI helper in the
// last 30 days (~/Library/Logs/DiagnosticReports/*.ips: the header, the
// exception and the top of the crashed thread, never the whole file), shows
// it all to edit, then opens a new GitHub issue in the browser with the
// bug report form's fields filled in (.github/ISSUE_TEMPLATE/bug_report.yml).
// Nothing is sent from here: the person submits the issue, or doesn't. Too
// long for a link, the report goes on the clipboard to paste instead.
// When settings open and macOS has kept a crash report since they last
// opened, a notice at the top offers to report it.
import AppKit
import SwiftUI

private let issuesURL = "https://github.com/bluevisor/haiku-pinyin/issues/new"
private let processNames: Set<String> = ["HaikuPinyin", "HaikuPinyinAI"]
private let keptFor: TimeInterval = 30 * 24 * 60 * 60
private let reportsShown = 5
private let framesShown = 8
private let longestURL = 8000		// GitHub answers 414 from about 8 KB
private let checkedKey = "crashesCheckedAt"

struct CrashReport {
	let file: URL
	let date: Date
	let process: String		// HaikuPinyin or HaikuPinyinAI
	let version: String		// "" for the helper
	let exception: String	// "EXC_BAD_ACCESS (SIGSEGV)", or "" when it doesn't say
	let summary: String		// a few lines, the home folder as ~
}

private func json(_ text: Substring) -> [String: Any]? {
	(try? JSONSerialization.jsonObject(with: Data(text.utf8))) as? [String: Any]
}

// One .ips: a line of JSON (the header), then the report's JSON.
func readCrashReport(_ file: URL, modified: Date) -> CrashReport? {
	guard let text = try? String(contentsOf: file, encoding: .utf8) else { return nil }
	let parts = text.split(separator: "\n", maxSplits: 1)
	guard let header = parts.first.flatMap(json) else { return nil }
	let process = header["app_name"] as? String ?? header["name"] as? String ?? ""
	guard processNames.contains(process) else { return nil }
	// The helper has no Info.plist, so no version of its own.
	let version = header["app_version"] as? String ?? ""
	let body = parts.count > 1 ? json(parts[1]) ?? [:] : [:]

	var lines = ["\(process)\(version.isEmpty ? "" : " " + version), \(header["timestamp"] as? String ?? "?"), "
		+ "\(header["os_version"] as? String ?? "?")"
		+ ((header["bug_type"] as? String).map { $0 == "309" ? "" : ", type \($0)" } ?? "")]
	var exception = ""
	if let info = body["exception"] as? [String: Any] {
		exception = [info["type"] as? String, (info["signal"] as? String).map { "(\($0))" }]
			.compactMap { $0 }.joined(separator: " ")
		if let subtype = info["subtype"] as? String { exception += ", " + subtype }
		lines.append(exception)
	}
	if let termination = (body["termination"] as? [String: Any])?["indicator"] as? String {
		lines.append("termination: \(termination)")
	}
	// What the crashing code said (a Swift fatal error, an assertion).
	if let asi = body["asi"] as? [String: [String]] {
		for (image, messages) in asi.sorted(by: { $0.key < $1.key }) {
			for message in messages.prefix(3) {
				lines.append("\(image): \(message.prefix(300))")
			}
		}
	}
	if let index = body["faultingThread"] as? Int,
		let threads = body["threads"] as? [[String: Any]], index < threads.count,
		let frames = threads[index]["frames"] as? [[String: Any]] {
		let images = body["usedImages"] as? [[String: Any]] ?? []
		lines.append("thread \(index) crashed:")
		for (number, frame) in frames.prefix(framesShown).enumerated() {
			let image = (frame["imageIndex"] as? Int).flatMap {
				$0 < images.count ? images[$0]["name"] as? String : nil } ?? "?"
			let symbol = frame["symbol"] as? String
				?? "0x" + String(frame["imageOffset"] as? Int ?? 0, radix: 16)
			lines.append("  \(number)  \(image)  \(symbol)")
		}
	}
	lines.append("(\(file.lastPathComponent))")
	return CrashReport(file: file, date: modified, process: process, version: version,
		exception: exception,
		summary: lines.joined(separator: "\n")
			.replacingOccurrences(of: NSHomeDirectory(), with: "~"))
}

// The method's and the helper's, newest first.
func crashReports() -> [CrashReport] {
	let folder = URL(fileURLWithPath: NSHomeDirectory())
		.appendingPathComponent("Library/Logs/DiagnosticReports")
	let files = (try? FileManager.default.contentsOfDirectory(at: folder,
		includingPropertiesForKeys: [.contentModificationDateKey])) ?? []
	let since = Date().addingTimeInterval(-keptFor)
	return files.compactMap { file -> CrashReport? in
		guard file.lastPathComponent.hasPrefix("HaikuPinyin"),
			file.pathExtension == "ips",
			let date = try? file.resourceValues(forKeys: [.contentModificationDateKey])
				.contentModificationDate, date > since else { return nil }
		return readCrashReport(file, modified: date)
	}.sorted { $0.date > $1.date }
}

// What the preview starts with: the diagnostics, then the crash reports.
func problemReport(_ crashes: [CrashReport]) -> String {
	var text = diagnosticsReport()
	text += "\n## Crash reports from the last 30 days\n"
	if crashes.isEmpty {
		text += "none\n"
	}
	for crash in crashes.prefix(reportsShown) {
		text += "\n" + crash.summary + "\n"
	}
	if crashes.count > reportsShown {
		text += "\n(and \(crashes.count - reportsShown) older)\n"
	}
	return text
}

// #pragma mark - the issue

private func encoded(_ text: String) -> String {
	let unreserved = CharacterSet(charactersIn:
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~")
	return text.addingPercentEncoding(withAllowedCharacters: unreserved) ?? ""
}

private var systemVersion: String {
	let version = ProcessInfo.processInfo.operatingSystemVersion
	var text = "macOS \(version.majorVersion).\(version.minorVersion)"
	if version.patchVersion > 0 { text += ".\(version.patchVersion)" }
	var size = 0
	sysctlbyname("kern.osversion", nil, &size, nil, 0)
	var build = [CChar](repeating: 0, count: max(size, 1))
	if size > 0, sysctlbyname("kern.osversion", &build, &size, nil, 0) == 0 {
		text += " (\(String(cString: build)))"
	}
	return text
}

// The new issue's link, with the form's text fields filled in by their ids.
// GitHub doesn't fill in a dropdown from a link (platform, layout): those
// are left to pick there.
func issueLink(title: String, happened: String, report: String) -> String {
	var fields: [(String, String)] = [
		("template", "bug_report.yml"),
		("version", Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString")
			as? String ?? ""),
		("system", systemVersion),
	]
	if !title.isEmpty { fields.append(("title", title)) }
	if !happened.isEmpty { fields.append(("got", happened)) }
	if !report.isEmpty { fields.append(("diagnostics", report)) }
	return issuesURL + "?" + fields.map { "\($0.0)=\(encoded($0.1))" }
		.joined(separator: "&")
}

// Opens the issue. False when it was too long, so some went on the
// clipboard instead.
func openIssue(title: String, happened: String, report: String) -> Bool {
	var link = issueLink(title: title, happened: happened, report: report)
	var clipboard = ""
	if link.count > longestURL {
		link = issueLink(title: title, happened: happened, report: "")
		clipboard = report
		if link.count > longestURL {
			link = issueLink(title: title, happened: "", report: "")
			clipboard = happened + "\n\n" + report
		}
	}
	if !clipboard.isEmpty {
		NSPasteboard.general.clearContents()
		NSPasteboard.general.setString(clipboard, forType: .string)
	}
	if let url = URL(string: link) { NSWorkspace.shared.open(url) }
	return clipboard.isEmpty
}

// #pragma mark - the notice

final class CrashNotice: ObservableObject {
	static let shared = CrashNotice()
	@Published var newest: CrashReport?

	// As settings open: a crash report kept since they last did?
	func settingsOpened() {
		let defaults = UserDefaults.standard
		let checked = defaults.object(forKey: checkedKey) as? Date ?? .distantPast
		if let crash = crashReports().first, crash.date > checked {
			newest = crash
		}
		defaults.set(Date(), forKey: checkedKey)
	}
}

struct CrashNoticeRow: View {
	let crash: CrashReport
	var report: () -> Void
	var dismiss: () -> Void

	var body: some View {
		HStack(spacing: 8) {
			Tile(symbol: "exclamationmark.triangle.fill", color: .orange)
			VStack(alignment: .leading, spacing: 2) {
				Text(crash.process == "HaikuPinyinAI"
					? L("The AI helper quit unexpectedly")
					: L("Haiku Pinyin quit unexpectedly"))
				Text(crash.date.formatted(date: .abbreviated, time: .shortened))
					.font(.caption)
					.foregroundStyle(.secondary)
			}
			Spacer()
			Button(L("Report…"), action: report)
				.glassButton()
			Button(action: dismiss) {
				Image(systemName: "xmark")
			}
			.buttonStyle(.borderless)
			.accessibilityLabel(L("Dismiss"))
		}
	}
}

// #pragma mark - the preview

struct ReportSheet: View {
	var close: () -> Void
	private let crashes: [CrashReport]
	@State private var title: String
	@State private var happened = ""
	@State private var report: String
	@State private var status = ""

	init(close: @escaping () -> Void) {
		self.close = close
		let crashes = crashReports()
		self.crashes = crashes
		_report = State(initialValue: problemReport(crashes))
		_title = State(initialValue: crashes.first.map {
			"Crash: \($0.process)" + ($0.version.isEmpty ? "" : " \($0.version)")
				+ ($0.exception.isEmpty ? "" : " (\($0.exception))") } ?? "")
	}

	var body: some View {
		VStack(alignment: .leading, spacing: 12) {
			HStack(spacing: 10) {
				Tile(symbol: "ladybug.fill", color: .orange)
				Text(L("Report a Problem"))
					.font(.headline)
			}
			Text(L("This opens a new issue on GitHub in your browser, filled in with what's below. Nothing is sent from here: edit or delete anything, then submit it there or don't."))
				.foregroundStyle(.secondary)
				.fixedSize(horizontal: false, vertical: true)
			VStack(alignment: .leading, spacing: 4) {
				Text(L("Title"))
					.font(.callout.weight(.medium))
				TextField(L("Title"), text: $title)
			}
			VStack(alignment: .leading, spacing: 4) {
				Text(L("What happened"))
					.font(.callout.weight(.medium))
				TextEditor(text: $happened)
					.font(.body)
					.frame(height: 56)
					.overlay(RoundedRectangle(cornerRadius: 5)
						.stroke(Color(nsColor: .separatorColor)))
			}
			VStack(alignment: .leading, spacing: 4) {
				Text(crashes.isEmpty ? L("In the issue: the diagnostics")
					: L("In the issue: the diagnostics and crash reports (%ld)",
						min(crashes.count, reportsShown)))
					.font(.callout.weight(.medium))
				TextEditor(text: $report)
					.font(.system(.caption, design: .monospaced))
					.frame(height: 260)
					.overlay(RoundedRectangle(cornerRadius: 5)
						.stroke(Color(nsColor: .separatorColor)))
				Text(L("Never what you type or the words it learned. Your home folder shows as ~."))
					.font(.caption)
					.foregroundStyle(.secondary)
			}
			if !status.isEmpty {
				Text(status)
					.font(.callout)
					.foregroundStyle(.orange)
					.fixedSize(horizontal: false, vertical: true)
			}
			HStack {
				Button(L("Copy")) {
					NSPasteboard.general.clearContents()
					NSPasteboard.general.setString(report, forType: .string)
				}
				Spacer()
				Button(status.isEmpty ? L("Cancel") : L("Done"), action: close)
					.keyboardShortcut(.cancelAction)
				Button(L("Open on GitHub")) {
					if openIssue(title: title, happened: happened, report: report) {
						close()
					} else {
						status = L("That's too long for a link, so it's on the clipboard: paste it into the issue that opened, under Diagnostics.")
					}
				}
				.buttonStyle(.borderedProminent)
			}
		}
		.padding(20)
		.frame(width: 560)
	}
}
