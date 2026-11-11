// Dev only: opens a text window, switches to 海酷拼音, types through it and
// prints what arrived; then switches back.
//   macos/test-session.sh       builds it as an app, opens it, prints the result
// The keys are events the window posts to itself: they reach the input
// method as typing does, with no Accessibility permission. It takes focus
// for about half a minute and stops if it loses it.
import Carbon
import Cocoa

let chineseMode = "com.bluevisor.inputmethod.HaikuPinyin.Chinese"
let englishMode = "com.bluevisor.inputmethod.HaikuPinyin.English"

let keyCodes: [Character: CGKeyCode] = [
	"a": 0, "s": 1, "d": 2, "f": 3, "h": 4, "g": 5, "z": 6, "x": 7, "c": 8,
	"v": 9, "b": 11, "q": 12, "w": 13, "e": 14, "r": 15, "y": 16, "t": 17,
	"1": 18, "2": 19, "3": 20, "4": 21, "6": 22, "5": 23, "=": 24, "9": 25,
	"7": 26, "-": 27, "8": 28, "0": 29, "]": 30, "o": 31, "u": 32, "[": 33,
	"i": 34, "p": 35, "l": 37, "j": 38, "'": 39, "k": 40, ";": 41, "\\": 42,
	",": 43, "/": 44, "n": 45, "m": 46, ".": 47, " ": 49,
]
let kReturn: CGKeyCode = 36, kDelete: CGKeyCode = 51, kEscape: CGKeyCode = 53
let kSpace: CGKeyCode = 49, kShift: CGKeyCode = 56, kTab: CGKeyCode = 48

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

	// A capital: the key with Shift.
	func shifted(_ character: Character) {
		focused()
		guard let code = keyCodes[character] else { finish("no key for \(character)") }
		post(kShift, true, .shift)
		post(code, true, .shift)
		post(code, false, .shift)
		post(kShift, false)
		usleep(120_000)
	}

	// The AI's gray completion: the input method's window just after the
	// cursor, on its line. "" when it's there.
	func ghostPlacement() -> String {
		var placed = ""
		for _ in 0..<15 {	// the model's answer comes on the side
			placed = ghostAt()
			if placed.isEmpty { break }
			usleep(200_000)
		}
		return placed
	}

	func ghostAt() -> String {
		let caret = onMain { () -> NSRect in
			let range = text.selectedRange()
			return text.firstRect(forCharacterRange: NSRange(location: range.location,
				length: 0), actualRange: nil)
		}
		let screenHeight = onMain { NSScreen.screens[0].frame.height }
		let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID)
			as? [[String: Any]] ?? []
		// By process: the owner's name is localized (海酷拼音).
		let pids = Set(NSWorkspace.shared.runningApplications
			.filter { $0.bundleIdentifier == "com.bluevisor.inputmethod.HaikuPinyin" }
			.map { $0.processIdentifier })
		for window in windows where pids.contains(
			window[kCGWindowOwnerPID as String] as? pid_t ?? 0) {
			guard let bounds = window[kCGWindowBounds as String] as? [String: CGFloat],
				let x = bounds["X"], let y = bounds["Y"], let height = bounds["Height"]
			else { continue }
			// Window list coordinates run down from the top of the main screen.
			let bottom = screenHeight - y - height
			if abs(x - caret.minX) <= 6 && bottom < caret.maxY && bottom + height > caret.minY {
				return ""
			}
		}
		return "no gray completion at the cursor \(caret)"
	}

	// The English layout under the method (U.S., ABC…).
	func ascii() -> String {
		onMain { () -> String in
			let source = TISCopyCurrentASCIICapableKeyboardLayoutInputSource().takeRetainedValue()
			let id = TISGetInputSourceProperty(source, kTISPropertyInputSourceID)
			return id.map { Unmanaged<CFString>.fromOpaque($0).takeUnretainedValue() as String } ?? "?"
		}
	}

	func shiftTap() {
		focused()
		post(kShift, true, .shift)
		usleep(50_000)
		post(kShift, false)
		usleep(400_000)
	}

	func currentSource() -> String {
		onMain { () -> String in
			let source = TISCopyCurrentKeyboardInputSource().takeRetainedValue()
			let id = TISGetInputSourceProperty(source, kTISPropertyInputSourceID)
			return id.map { Unmanaged<CFString>.fromOpaque($0).takeUnretainedValue() as String } ?? "?"
		}
	}

	// The input source the system has selected now: the menu bar shows its icon.
	func expectSource(_ id: String, _ when: String) {
		var current = ""
		for _ in 0..<20 {
			current = currentSource()
			if current == id { return }
			usleep(100_000)
		}
		finish("\(when): the input source is \(current), not \(id)")
	}

	func select(_ id: String) {
		onMain { if let source = inputSource(id) { _ = TISSelectInputSource(source) } }
		usleep(500_000)
	}

	var previous: TISInputSource?
	var keep = false	// leaves the source as the test did, for the next one
	var report = ""
	let output = CommandLine.arguments.count > 1 ? CommandLine.arguments[1]
		: "/dev/stdout"

	func finish(_ message: String) -> Never {
		onMain {
			if let previous, !keep || message != "done" { TISSelectInputSource(previous) }
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
		// "carry" goes on from where "modes" left off, in another app.
		let start = phase == "carry" ? englishMode : chineseMode
		onMain {
			previous = TISCopyCurrentKeyboardInputSource().takeRetainedValue()
			keep = phase == "modes"
			if phase == "carry" {
				previous = TISCopyCurrentASCIICapableKeyboardLayoutInputSource().takeRetainedValue()
			} else if let source = inputSource(chineseMode) {
				TISSelectInputSource(source)
			}
		}
		usleep(300_000)
		let current = currentSource()
		guard current == start else { finish("input source is \(current), not \(start)") }
		usleep(800_000)

		if phase == "shuangpin" {
			shuangpin()
		} else if phase == "live" {
			live()
		} else if phase == "modes" {
			modes()
		} else if phase == "carry" {
			t("hello"); k(kReturn)			// English, as the last app left it
			shiftTap()
			expectSource(chineseMode, "after a Shift tap")
			t("nihao"); k(kSpace)			// 你好
		} else if phase == "ai" {
			ai()
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
		t("nihao"); k(keyCodes["1"]!)		// 你好
		t(".")								// 。
		t("bjdx"); k(kReturn)				// bjdx, as typed
		t("zhongguo"); k(kEscape)			// nothing
		t("shuru"); k(keyCodes["]"]!)		// 入 (以词定字)
		k(kReturn)
		t("wozaixuexizhongwen"); k(kSpace)	// a sentence
		t("shengrikuaile"); k(kSpace)		// 生日快乐
		k(kReturn)
		t("xiexie"); k(keyCodes["="]!, keyCodes["-"]!, keyCodes["1"]!)	// page, back, 谢谢
		t("nihaoma"); k(kDelete, kDelete, kSpace)	// 你好
		k(kReturn)
		t("nihao1.")						// 你好。 (period after a digit pick)
		t("v12b")							// 壹拾贰: v-mode, letters pick
		t("vjt2")							// ←: a code, digits pick
		t("v1"); shifted("="); t("2"); shifted("8"); t("3"); k(kSpace)	// 7
	}

	// The AI on: it reads what's before the cursor. Its answers come on the
	// side, so each step waits for one.
	func ai() {
		usleep(6_000_000)					// the model loads
		t("womeimeishuo"); k(kSpace)		// 我妹妹说
		t("ta"); usleep(800_000); k(kSpace)	// 她, not 他: the AI moved it
		k(kReturn)
		t("zhunishengri"); k(kSpace)		// 祝你生日
		usleep(800_000); k(kTab)			// 快乐, the AI's completion
		usleep(800_000); k(kReturn)			// a newline, past any other
		t("zhonghuarenmin"); k(kSpace)		// 中华人民
		usleep(1_000_000)
		let placed = ghostPlacement()		// 共和国 in gray after the cursor
		if !placed.isEmpty { finish(placed) }
		k(kEscape)							// gone, never typed
		t("beijingda"); k(kSpace)			// 北京大
		usleep(800_000); t(",")				// typing on: the gray 学 goes
	}

	// A setting changed from outside (`defaults write`, as a sync tool would)
	// takes effect while it runs: it starts in 拼音加加.
	func live() {
		t("vygo"); k(kSpace)				// 中国, in 拼音加加
		setting("layout", "full")
		t("zhongguo"); k(kSpace)			// 中国, in full pinyin
		t("nihao"); k(kSpace)				// 你好
		setting("layout", "pyjj")
		t("vygo"); k(kSpace)				// 中国
	}

	func setting(_ key: String, _ value: String) {
		let task = Process()
		task.executableURL = URL(fileURLWithPath: "/usr/bin/defaults")
		task.arguments = ["write", "com.bluevisor.inputmethod.HaikuPinyin", key, value]
		try? task.run()
		task.waitUntilExit()
		usleep(500_000)
	}

	// 中 and 英 are two input modes, so the menu bar shows which is on: the
	// Shift tap selects the other, and the engine follows the one selected.
	func modes() {
		t("nihao"); k(kSpace)				// 你好
		shiftTap()
		expectSource(englishMode, "after a Shift tap")
		t("hello ")							// English
		shiftTap()
		expectSource(chineseMode, "after a second Shift tap")
		t("nihao"); k(kSpace)				// 你好
		k(kReturn)
		// Away to another source and back, in English: still English.
		shiftTap()
		expectSource(englishMode, "after a third Shift tap")
		select(ascii())
		t("abc ")
		select(englishMode)
		expectSource(englishMode, "back to the 英 mode")
		t("world ")
		// The input menu lists only 中: choosing it types Chinese again.
		select(chineseMode)
		expectSource(chineseMode, "back to the 中 mode")
		t("zhongguo"); k(kSpace)			// 中国
		// Left in English for "carry".
		shiftTap()
		expectSource(englishMode, "after the last Shift tap")
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
		shifted("v"); t("12b")				// 壹拾贰: Shift+V is v-mode here
	}
}

let phase = CommandLine.arguments.count > 2 ? CommandLine.arguments[2] : "full"
let app = NSApplication.shared
let pad = Pad()
app.delegate = pad
app.setActivationPolicy(.regular)
app.run()
