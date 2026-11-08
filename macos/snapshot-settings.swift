// Dev only: renders the settings window's view to a PNG, to look at it
// without screen recording permission:
//   swiftc -parse-as-library macos/Settings.swift macos/Candidates.swift \
//       macos/Models.swift macos/Updater.swift macos/snapshot-settings.swift \
//       -o build/snapshot-settings
//   build/snapshot-settings out.png [light|dark|bubble|candidates[-dark]]
// It flashes the window on screen for a second.
import AppKit
import SwiftUI

// What the input method's own code provides the settings window.
@_cdecl("HPAIModelsChanged") func aiModelsChangedStub() {}
@_cdecl("HPSaveNow") func saveNowStub() {}
@_cdecl("HPComposing") func composingStub() -> Bool { false }

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
		if arguments.count > 2 && arguments[2] == "bubble" {
			// The 中/英 bubble, over a window to show the glass against.
			let backdrop = NSWindow(contentRect: NSRect(x: 400, y: 400, width: 160, height: 90),
				styleMask: [.borderless], backing: .buffered, defer: false)
			backdrop.backgroundColor = .systemOrange
			backdrop.orderFrontRegardless()
			showModeBubble(true, 440, 470)
			RunLoop.main.run(until: Date().addingTimeInterval(0.3))
			capture(NSApp.windows.first { $0 is NSPanel }!, to: arguments[1])
			return
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
