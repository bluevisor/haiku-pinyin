// Dev only: opens a text window, switches to 海酷拼音, types through it and
// prints what arrived; then switches back.
//   macos/test-session.sh       builds it as an app, opens it, prints the result
// The keys are events the window posts to itself: they reach the input
// method as typing does, with no Accessibility permission. It takes focus
// for about half a minute and stops if it loses it.
import Carbon
import Cocoa

let chineseMode = "com.bluevisor.inputmethod.HaikuPinyin.Chinese"

let keyCodes: [Character: CGKeyCode] = [
	"a": 0, "s": 1, "d": 2, "f": 3, "h": 4, "g": 5, "z": 6, "x": 7, "c": 8,
	"v": 9, "b": 11, "q": 12, "w": 13, "e": 14, "r": 15, "y": 16, "t": 17,
	"1": 18, "2": 19, "3": 20, "4": 21, "6": 22, "5": 23, "=": 24, "9": 25,
	"7": 26, "-": 27, "8": 28, "0": 29, "]": 30, "o": 31, "u": 32, "[": 33,
	"i": 34, "p": 35, "l": 37, "j": 38, "'": 39, "k": 40, ";": 41, "\\": 42,
	",": 43, "/": 44, "n": 45, "m": 46, ".": 47, " ": 49,
]
let kReturn: CGKeyCode = 36, kDelete: CGKeyCode = 51, kEscape: CGKeyCode = 53
let kSpace: CGKeyCode = 49, kShift: CGKeyCode = 56

func inputSource(_ id: String) -> TISInputSource? {
	let filter = [kTISPropertyInputSourceID as String: id] as CFDictionary
	let list = TISCreateInputSourceList(filter, true)?.takeRetainedValue()
	return (list as? [TISInputSource])?.first
}

final class Pad: NSObject, NSApplicationDelegate {
	var window: NSWindow!
	var text: NSTextView!

	func applicationDidFinishLaunching(_ notification: Notification) {
		window = NSWindow(contentRect: NSRect(x: 240, y: 240, width: 640, height: 240),
			styleMask: [.titled], backing: .buffered, defer: false)
		window.title = "hp-session-test"
		let scroll = NSTextView.scrollableTextView()
		text = scroll.documentView as? NSTextView
		text.font = NSFont.systemFont(ofSize: 20)
		window.contentView = scroll
		window.makeKeyAndOrderFront(nil)
		window.makeFirstResponder(text)
		NSApp.activate(ignoringOtherApps: true)
		Thread { self.run() }.start()
	}

	func onMain<T>(_ work: () -> T) -> T { DispatchQueue.main.sync(execute: work) }

	func focused() {
		if onMain({ NSApp.isActive && window.isKeyWindow }) { return }
		let state = onMain { "active \(NSApp.isActive) key \(window.isKeyWindow), "
			+ "front \(NSWorkspace.shared.frontmostApplication?.localizedName ?? "?"), "
			+ "typed so far \(text.string.debugDescription)" }
		finish("the test window lost focus; stopped (\(state))")
	}

	let events = CGEventSource(stateID: .privateState)

	// Real key events (with a CGEvent inside, as text input needs), handed to
	// this app's own event queue: no Accessibility permission.
	func post(_ code: CGKeyCode, _ down: Bool, _ flags: NSEvent.ModifierFlags = [],
		_ character: String = "") {
		onMain {
			let cg = CGEvent(keyboardEventSource: events, virtualKey: code, keyDown: down)!
			cg.flags = CGEventFlags(rawValue: UInt64(flags.rawValue))
			if code == kShift { cg.type = .flagsChanged }
			if let event = NSEvent(cgEvent: cg) { NSApp.postEvent(event, atStart: false) }
		}
	}

	static let specials: [CGKeyCode: String] = [36: "\r", 51: "\u{7f}", 53: "\u{1b}", 49: " "]

	func press(_ code: CGKeyCode, _ character: String) {
		post(code, true, [], character)
		post(code, false, [], character)
	}

	func k(_ codes: CGKeyCode...) {
		for code in codes {
			focused()
			let character = Pad.specials[code]
				?? keyCodes.first(where: { $0.value == code }).map { String($0.key) } ?? ""
			press(code, character)
			usleep(120_000)
		}
		usleep(200_000)
	}

	func t(_ string: String) {
		for character in string {
			focused()
			guard let code = keyCodes[character] else { finish("no key for \(character)") }
			press(code, String(character))
			usleep(60_000)
		}
		usleep(250_000)
	}

	func shiftTap() {
		focused()
		post(kShift, true, .shift)
		usleep(50_000)
		post(kShift, false)
		usleep(400_000)
	}

	var previous: TISInputSource?
	var report = ""
	let output = CommandLine.arguments.count > 1 ? CommandLine.arguments[1]
		: "/dev/stdout"

	func finish(_ message: String) -> Never {
		onMain {
			if let previous { TISSelectInputSource(previous) }
			report += message + "\n"
			try? report.write(toFile: output, atomically: true, encoding: .utf8)
			print(report, terminator: "")
			exit(message == "done" ? 0 : 1)
		}
		fatalError()
	}

	func run() {
		// Opened by `open`, the window gets focus once launching ends.
		for _ in 0..<50 where !onMain({ NSApp.isActive && window.isKeyWindow }) {
			usleep(100_000)
		}
		onMain {
			previous = TISCopyCurrentKeyboardInputSource().takeRetainedValue()
			if let source = inputSource(chineseMode) { TISSelectInputSource(source) }
		}
		usleep(300_000)
		let current = onMain { () -> String in
			let source = TISCopyCurrentKeyboardInputSource().takeRetainedValue()
			let id = TISGetInputSourceProperty(source, kTISPropertyInputSourceID)
			return id.map { Unmanaged<CFString>.fromOpaque($0).takeUnretainedValue() as String } ?? "?"
		}
		guard current == chineseMode else { finish("input source is \(current), not 海酷拼音") }
		usleep(800_000)

		if phase == "shuangpin" {
			shuangpin()
		} else {
			fullPinyin()
		}
		usleep(500_000)

		let typed = onMain { text.string }
		report += typed + "\n"
		finish("done")
	}
}

extension Pad {
	// Default settings.
	func fullPinyin() {
		t("zgr"); k(kSpace)					// 中国人 (abbreviation)
		t(",")								// ，
		shiftTap(); t("hello "); shiftTap()	// English mode
		t("nihao"); k(keyCodes["1"]!)		// 你好, then 联想 shows
		t(".")								// 。 (联想 dropped)
		t("bjdx"); k(kReturn)				// bjdx, as typed
		t("zhongguo"); k(kEscape)			// nothing
		t("shuru"); k(keyCodes["]"]!)		// 入 (以词定字)
		k(kReturn)
		t("wozaixuexizhongwen"); k(kSpace)	// a sentence
		t("shengri"); k(kSpace); k(keyCodes["1"]!)	// 生日, then 联想 1 (快乐)
		k(kReturn)
		t("xiexie"); k(keyCodes["="]!, keyCodes["-"]!, keyCodes["1"]!)	// page, back, 谢谢
		t("nihaoma"); k(kDelete, kDelete, kSpace)	// 你好
		k(kReturn)
		t("nihao1.")						// 你好。 (period after a digit pick)
	}

	// 拼音加加, a o e one key each.
	func shuangpin() {
		t("vygo"); k(kSpace)				// 中国
		t(",")								// ，
		t("iawfyieqawwirme"); k(kSpace)		// 啥玩意儿啊为什么: a lone a
		k(kReturn)
		t("nihd"); k(kReturn)				// nihd, as typed
		t("iurufa"); k(kSpace)				// 输入法 (sh = i)
		t("vyg"); k(kSpace)					// 中国: the last key alone
		k(kReturn)
		t("ewo"); k(kSpace)					// 饿我: a lone e
	}
}

let phase = CommandLine.arguments.count > 2 ? CommandLine.arguments[2] : "full"
let app = NSApplication.shared
let pad = Pad()
app.delegate = pad
app.setActivationPolicy(.regular)
app.run()
