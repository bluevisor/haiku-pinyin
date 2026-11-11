// Dev only: renders the settings window's view to a PNG, to look at it
// without screen recording permission:
//   mkdir -p build/snapshot && cp -R macos/Resources/*.lproj build/snapshot/
//   swiftc -parse-as-library macos/Settings.swift macos/Candidates.swift \
//       macos/Models.swift macos/Updater.swift macos/Strings.swift \
//       macos/Invitation.swift macos/Diagnostics.swift macos/Words.swift \
//       macos/Report.swift macos/Uninstall.swift \
//       macos/snapshot-settings.swift -o build/snapshot/snapshot-settings
//   build/snapshot/snapshot-settings out.png \
//       [light|dark|bubble[-english]|candidates[-dark]|invitation[-dark]
//        |report[-dark]|uninstall[-dark]]
//   build/snapshot/snapshot-settings out.txt diagnostics    what Copy copies
//   build/snapshot/snapshot-settings out.txt report-text  Report a Problem's
//       text and the issue's link (SNAPSHOT_CRASH=1 shows the settings
//       window's crash notice, with the newest real crash report)
//   build/snapshot/snapshot-settings out.txt uninstall-plan [keep|all]
//       the uninstall script for this Mac, as a dry run: nothing deleted
//   build/snapshot/snapshot-settings out.txt uninstall-test <home>
//       the uninstall script, with the data, for real, on a scratch copy
//       of a home folder (and $HAIKU_PINYIN_MODELS, and the defaults domain
//       com.bluevisor.inputmethod.HaikuPinyin.uninstall-test); it waits for
//       this process to exit, as for the method, so check after a second
//   build/snapshot/snapshot-settings out.txt accessibility
// The last reads the candidate window as VoiceOver does (AXUIElement, on
// this process: no Accessibility permission), presses a candidate, and
// lists what was announced; it exits 1 if something's off (in English).
// It flashes the window on screen for a second. The string tables sit next
// to it, so -AppleLanguages '(zh-Hant)' (or en, zh-Hans) at the end picks
// the language.
import AppKit
import SwiftUI

// What the input method's own code provides the settings window.
// SNAPSHOT_AI=failed or missing shows the AI as a broken or missing model
// leaves it; HAIKU_PINYIN_AI=/usr/bin/true stands in for the helper, and
// HAIKU_PINYIN_MODELS=<empty folder> shows the download.
private let aiState = ProcessInfo.processInfo.environment["SNAPSHOT_AI"] ?? ""
@_cdecl("HPAIModelsChanged") func aiModelsChangedStub() {}
@_cdecl("HPAIUsable") func aiUsableStub() -> Bool { aiState.isEmpty }
@_cdecl("HPAIFailed") func aiFailedStub() -> Bool { aiState == "failed" }
@_cdecl("HPAIReady") func aiReadyStub() -> Bool { aiState.isEmpty }
@_cdecl("HPAIHelperProcess") func aiHelperProcessStub() -> Int32 { 0 }
@_cdecl("HPSaveNow") func saveNowStub() {}
@_cdecl("HPComposing") func composingStub() -> Bool { false }
@_cdecl("HPPhrasesFile") func phrasesFileStub() -> UnsafePointer<CChar> {
	UnsafePointer(strdup("/tmp/phrases.txt")!)
}
@_cdecl("HPImportWords") func importWordsStub(_ path: UnsafePointer<CChar>,
	_ words: UnsafeMutablePointer<Int32>, _ phrases: UnsafeMutablePointer<Int32>,
	_ skipped: UnsafeMutablePointer<Int32>, _ problem: UnsafeMutablePointer<Int32>)
	-> UnsafePointer<CChar> { UnsafePointer(strdup("")!) }
@_cdecl("HPExportWords") func exportWordsStub(_ path: UnsafePointer<CChar>) -> Int32 { 0 }

@main
struct SnapshotSettings {
	static func main() {
		let arguments = CommandLine.arguments
		guard arguments.count > 1 else {
			print("usage: snapshot-settings out.png [light|dark]")
			exit(1)
		}
		let app = NSApplication.shared
		app.setActivationPolicy(.accessory)
		let dark = arguments.count > 2 && arguments[2] == "dark"

		if arguments.count > 2 && arguments[2].hasPrefix("candidates") {
			// A page of candidates over a backdrop.
			let backdrop = NSWindow(contentRect: NSRect(x: 380, y: 380, width: 640, height: 120),
				styleMask: [.borderless], backing: .buffered, defer: false)
			backdrop.backgroundColor = .systemTeal
			backdrop.appearance = NSAppearance(named: arguments[2].hasSuffix("dark")
				? .darkAqua : .aqua)
			NSApp.appearance = backdrop.appearance
			backdrop.orderFrontRegardless()
			showCandidates("啥玩意兒\n啥玩意\n殺完\n沙灣\n傻", 0, 2, 0, false, true,
				420, 470, 20)
			RunLoop.main.run(until: Date().addingTimeInterval(0.5))
			capture(NSApp.windows.first { $0 is NSPanel }!, to: arguments[1])
			return
		}
		if arguments.count > 2 && arguments[2] == "accessibility" {
			AccessibilityCheck.run(to: arguments[1])
		}
		if arguments.count > 2 && arguments[2] == "report-text" {
			let report = problemReport(crashReports())
			let text = report + "\n## Link\n" + issueLink(title: "Test", happened: "It crashed.",
				report: report) + "\n"
			FileManager.default.createFile(atPath: arguments[1], contents: text.data(using: .utf8))
			return
		}
		if arguments.count > 2 && arguments[2] == "uninstall-plan" {
			var plan = UninstallPlan.current(removeData: arguments.count > 3 && arguments[3] == "all")
			plan.app = URL(fileURLWithPath: NSHomeDirectory())
				.appendingPathComponent("Library/Input Methods/HaikuPinyin.app")
			plan.domain = "com.bluevisor.inputmethod.HaikuPinyin"
			plan.dryRun = true
			FileManager.default.createFile(atPath: arguments[1], contents:
				plan.script(waitingFor: getpid(), helper: 0).data(using: .utf8))
			return
		}
		if arguments.count > 3 && arguments[2] == "uninstall-test" {
			var plan = UninstallPlan.forHome(URL(fileURLWithPath: arguments[3]), removeData: true)
			plan.domain = "com.bluevisor.inputmethod.HaikuPinyin.uninstall-test"
			plan.receipt = nil
			plan.processName = nil
			let text = plan.script(waitingFor: getpid(), helper: 0)
			FileManager.default.createFile(atPath: arguments[1], contents: text.data(using: .utf8))
			guard plan.launch(waitingFor: getpid(), helper: 0) else {
				print("couldn't start the script")
				exit(1)
			}
			Thread.sleep(forTimeInterval: 0.5)
			return
		}
		if arguments.count > 2 && (arguments[2].hasPrefix("report")
				|| arguments[2].hasPrefix("uninstall")) {
			// A sheet, in a window of its own.
			let view = arguments[2].hasPrefix("report")
				? AnyView(ReportSheet {}) : AnyView(UninstallSheet {})
			let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 400, height: 400),
				styleMask: [.titled, .fullSizeContentView], backing: .buffered, defer: false)
			window.titlebarAppearsTransparent = true
			window.appearance = NSAppearance(named: dark || arguments[2].hasSuffix("dark")
				? .darkAqua : .aqua)
			let host = NSHostingView(rootView: view)
			window.contentView = host
			window.setContentSize(host.fittingSize)
			window.center()
			window.orderFrontRegardless()
			RunLoop.main.run(until: Date().addingTimeInterval(1))
			capture(window, to: arguments[1])
			return
		}
		if arguments.count > 2 && arguments[2] == "diagnostics" {
			FileManager.default.createFile(atPath: arguments[1],
				contents: diagnosticsReport().data(using: .utf8))
			return
		}
		if arguments.count > 2 && arguments[2].hasPrefix("invitation") {
			// The AI's invitation under a line of text.
			let backdrop = NSWindow(contentRect: NSRect(x: 380, y: 300, width: 400, height: 200),
				styleMask: [.borderless], backing: .buffered, defer: false)
			backdrop.backgroundColor = .textBackgroundColor
			backdrop.appearance = NSAppearance(named: arguments[2].hasSuffix("dark")
				? .darkAqua : .aqua)
			NSApp.appearance = backdrop.appearance
			backdrop.orderFrontRegardless()
			showInvitation(400, 470, 20)
			RunLoop.main.run(until: Date().addingTimeInterval(0.5))
			capture(NSApp.windows.first { $0 is NSPanel }!, to: arguments[1])
			return
		}
		if arguments.count > 2 && arguments[2].hasPrefix("bubble") {
			// The 中/英 bubble, over a window to show the glass against.
			let backdrop = NSWindow(contentRect: NSRect(x: 400, y: 400, width: 160, height: 90),
				styleMask: [.borderless], backing: .buffered, defer: false)
			backdrop.backgroundColor = .systemOrange
			backdrop.orderFrontRegardless()
			showModeBubble(!arguments[2].hasSuffix("english"), 440, 470)
			RunLoop.main.run(until: Date().addingTimeInterval(0.3))
			capture(NSApp.windows.first { $0 is NSPanel }!, to: arguments[1])
			return
		}

		// SNAPSHOT_AT=ai opens it at the AI section, as the invitation does.
		SettingsTarget.shared.section = ProcessInfo.processInfo.environment["SNAPSHOT_AT"]
		if ProcessInfo.processInfo.environment["SNAPSHOT_CRASH"] != nil {
			CrashNotice.shared.newest = crashReports().first
		}
		let host = NSHostingView(rootView: SettingsView())
		let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 480, height: 400),
			styleMask: [.titled], backing: .buffered, defer: false)
		styleSettingsWindow(window)
		window.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
		window.contentView = host
		// SNAPSHOT_HEIGHT=1800 shows the whole form.
		let height = Double(ProcessInfo.processInfo.environment["SNAPSHOT_HEIGHT"] ?? "") ?? 720
		window.setContentSize(NSSize(width: 480, height: height))
		window.center()
		window.orderFrontRegardless()
		RunLoop.main.run(until: Date().addingTimeInterval(1))
		// SNAPSHOT_SCROLL=300 scrolls the form first (what's under the title bar).
		if let by = Double(ProcessInfo.processInfo.environment["SNAPSHOT_SCROLL"] ?? ""),
			let scroll = scrollView(in: host) {
			scroll.contentView.scroll(to: NSPoint(x: 0, y: by))
			scroll.reflectScrolledClipView(scroll.contentView)
			RunLoop.main.run(until: Date().addingTimeInterval(0.5))
		}

		capture(window, to: arguments[1])
		print(arguments[1], window.frame.size)
	}

	static func scrollView(in view: NSView) -> NSScrollView? {
		if let scroll = view as? NSScrollView { return scroll }
		for sub in view.subviews {
			if let found = scrollView(in: sub) { return found }
		}
		return nil
	}

	// An app may capture its own windows without screen recording
	// permission. CGWindowListCreateImage is unavailable to Swift now: reach
	// it through dlsym.
	static func capture(_ window: NSWindow, to path: String) {
		typealias Capture = @convention(c) (CGRect, UInt32, UInt32, UInt32)
			-> Unmanaged<CGImage>?
		guard let symbol = dlsym(UnsafeMutableRawPointer(bitPattern: -2),
				"CGWindowListCreateImage"),
			let image = unsafeBitCast(symbol, to: Capture.self)(.null,
				1 << 3 /* including window */, UInt32(window.windowNumber),
				1 << 0 | 1 << 3 /* ignore framing, best resolution */)?
				.takeRetainedValue() else {
			print("no image")
			exit(1)
		}
		let png = NSBitmapImageRep(cgImage: image)
			.representation(using: .png, properties: [:])
		FileManager.default.createFile(atPath: path, contents: png)
	}
}

private var checkPicked: Int32 = -1		// what a press picked
private var heard: [String] = []		// announcements, as VoiceOver gets them
private let heardLock = NSLock()

// The candidate window through the accessibility API, from another thread
// (asking this process from its main thread can't be answered).
enum AccessibilityCheck {
	static var report: [String] = []
	static var failed = false

	static func run(to path: String) {
		announceAlways = true
		setCandidateHandlers({ checkPicked = $0 }, { _ in })
		showCandidates("中国\n中\n种", 0, 2, 0, false, true, 420, 470, 20, "123")
		Thread {
			check()
			DispatchQueue.main.async {
				report.append(failed ? "FAILED" : "OK")
				let text = report.joined(separator: "\n") + "\n"
				print(text, terminator: "")
				FileManager.default.createFile(atPath: path, contents: text.data(using: .utf8))
				exit(failed ? 1 : 0)
			}
		}.start()
		NSApp.run()
	}

	static func expect(_ ok: Bool, _ what: String) {
		report.append((ok ? "ok    " : "WRONG ") + what)
		if !ok { failed = true }
	}

	static func attribute(_ element: AXUIElement, _ name: String) -> CFTypeRef? {
		var value: CFTypeRef?
		AXUIElementCopyAttributeValue(element, name as CFString, &value)
		return value
	}

	static func string(_ element: AXUIElement, _ name: String) -> String {
		(attribute(element, name) as? String) ?? ""
	}

	static func children(_ element: AXUIElement) -> [AXUIElement] {
		(attribute(element, kAXChildrenAttribute) as? [AXUIElement]) ?? []
	}

	// Every element under element, depth first, with its depth.
	static func walk(_ element: AXUIElement, _ depth: Int = 0) -> [(AXUIElement, Int)] {
		[(element, depth)] + children(element).flatMap { walk($0, depth + 1) }
	}

	static func onMain<T>(_ work: () -> T) -> T { DispatchQueue.main.sync(execute: work) }

	// Listens as VoiceOver does, on a thread of its own.
	static func listen() {
		Thread {
			var observer: AXObserver?
			AXObserverCreateWithInfoCallback(getpid(), { _, _, _, info, _ in
				let text = (info as? [String: Any])?[NSAccessibility.NotificationUserInfoKey
					.announcement.rawValue] as? String ?? "?"
				heardLock.lock()
				heard.append(text)
				heardLock.unlock()
			}, &observer)
			guard let observer else { return }
			AXObserverAddNotification(observer, AXUIElementCreateApplication(getpid()),
				"AXAnnouncementRequested" as CFString, nil)
			CFRunLoopAddSource(CFRunLoopGetCurrent(), AXObserverGetRunLoopSource(observer),
				.defaultMode)
			CFRunLoopRun()
		}.start()
	}

	static func heardSoFar() -> [String] {
		heardLock.lock()
		defer { heardLock.unlock() }
		return heard
	}

	static func check() {
		Thread.sleep(forTimeInterval: 0.5)
		let app = AXUIElementCreateApplication(getpid())
		let windows = (attribute(app, kAXWindowsAttribute) as? [AXUIElement]) ?? []
		expect(windows.count == 1, "one window: \(windows.count)")
		guard let window = windows.first else { return }
		for (element, depth) in walk(window) {
			var line = String(repeating: "  ", count: depth) + string(element, kAXRoleAttribute)
			let label = string(element, kAXDescriptionAttribute)
			if !label.isEmpty { line += " \"\(label)\"" }
			if (attribute(element, kAXSelectedAttribute) as? Bool) == true { line += " selected" }
			report.append(line)
		}
		let buttons = walk(window).map(\.0).filter {
			string($0, kAXRoleAttribute) == kAXButtonRole as String }
		let labels = buttons.map { string($0, kAXDescriptionAttribute) }
		expect(labels.prefix(3) == ["中国, 1 of 3, AI pick", "中, 2 of 3", "种, 3 of 3, from the cloud"],
			"the candidates are buttons, in order: \(labels)")
		expect(labels.contains("Next page") && !labels.contains("Previous page"),
			"the page arrow that works, and not the other")
		expect((attribute(buttons[0], kAXSelectedAttribute) as? Bool) == true
			&& (attribute(buttons[1], kAXSelectedAttribute) as? Bool) != true,
			"the highlighted one is selected, the others aren't")
		expect(onMain { announced } == "中国, 1 of 3, AI pick",
			"announced as it showed: \(onMain { announced })")

		// The highlight moves on: announced once; again with no change: not.
		listen()
		Thread.sleep(forTimeInterval: 0.3)
		for _ in 0 ..< 2 {
			onMain { showCandidates("中国\n中\n种", 1, 2, 0, false, true, 420, 470, 20, "123") }
			Thread.sleep(forTimeInterval: 0.3)
		}
		expect(heardSoFar() == ["中, 2 of 3"],
			"the next one announced to accessibility clients, once: \(heardSoFar())")

		// VoiceOver's press picks.
		AXUIElementPerformAction(buttons[2], kAXPressAction as CFString)
		Thread.sleep(forTimeInterval: 0.3)
		expect(onMain { checkPicked } == 2, "pressing the third picks it: \(onMain { checkPicked })")

		// It never takes the keyboard.
		let focus = onMain { () -> (Bool, Bool, Bool) in
			let panel = NSApp.windows.first { $0 is NSPanel }!
			panel.makeKey()
			return (panel.canBecomeKey, panel.isKeyWindow, NSApp.keyWindow != nil)
		}
		expect(!focus.0 && !focus.1 && !focus.2,
			"the window can't become key, even when asked: \(focus)")
		expect((attribute(window, kAXMainAttribute) as? Bool) != true
			&& (attribute(window, kAXFocusedAttribute) as? Bool) != true,
			"not main or focused for VoiceOver either")
	}
}
