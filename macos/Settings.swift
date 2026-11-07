// 海酷拼音 Haiku Pinyin for macOS: the settings window, and the 中/英 bubble
// a Shift tap shows. Settings live in the method's user defaults; the input
// controller applies them as they change.
import AppKit
import SwiftUI

private let layouts: [(id: String, name: String)] = [
	("full", "全拼"),
	("pyjj", "双拼 · 拼音加加"),
	("xiaohe", "双拼 · 小鹤"),
	("ziranma", "双拼 · 自然码"),
	("mspy", "双拼 · 微软"),
	("abc", "双拼 · 智能ABC"),
]

// The engine's Fuzzy masks (engine/Spelling.h).
private let fuzzyGroups: [(mask: Int, name: String)] = [
	(1, "z = zh   c = ch   s = sh"),
	(2, "an = ang   en = eng   in = ing"),
	(4 | 8 | 16, "n = l   f = h   r = l"),
]

private struct SettingsView: View {
	@AppStorage("layout") private var layout = "full"
	@AppStorage("script") private var script = 0
	@AppStorage("pageSize") private var pageSize = 7
	@AppStorage("fullwidth") private var fullwidth = true
	@AppStorage("emoji") private var emoji = true
	@AppStorage("prediction") private var prediction = true
	@AppStorage("english") private var english = true
	@AppStorage("cloud") private var cloud = false
	@AppStorage("fuzzy") private var fuzzy = 0

	private func fuzzyGroup(_ mask: Int) -> Binding<Bool> {
		Binding(get: { fuzzy & mask == mask },
			set: { fuzzy = $0 ? fuzzy | mask : fuzzy & ~mask })
	}

	var body: some View {
		Form {
			Section {
				Picker("拼写方式", selection: $layout) {
					ForEach(layouts, id: \.id) { Text($0.name).tag($0.id) }
				}
				Picker("字形", selection: $script) {
					Text("简体中文").tag(0)
					Text("繁體中文").tag(1)
				}
				Picker("每页候选", selection: $pageSize) {
					ForEach([5, 7, 9], id: \.self) { Text("\($0) 个").tag($0) }
				}
			}
			Section {
				Toggle("联想", isOn: $prediction)
				Toggle("中英混输", isOn: $english)
				Toggle("Emoji 候选", isOn: $emoji)
				Toggle("全角标点", isOn: $fullwidth)
			} footer: {
				Text("联想：上屏后列出可能接着输入的词，按数字键选择。中英混输：hello 也给出 hello。")
					.foregroundStyle(.secondary)
			}
			Section("模糊音") {
				ForEach(fuzzyGroups, id: \.mask) {
					Toggle($0.name, isOn: fuzzyGroup($0.mask))
				}
			}
			Section {
				Toggle("云候选 (Google)", isOn: $cloud)
			} footer: {
				Text("把正在输入的拼音发给 Google 输入工具，不含已上屏的文字。")
					.foregroundStyle(.secondary)
			}
			Section {
				LabeledContent("学到的词") {
					Button("在访达中显示") { revealUserDictionary() }
				}
			} footer: {
				Text("轻按 Shift 切换中 / 英；在候选上按 Control-Delete 忘掉学到的词。")
					.foregroundStyle(.secondary)
			}
		}
		.formStyle(.grouped)
		.frame(width: 460)
		.fixedSize(horizontal: false, vertical: true)
	}

	private func revealUserDictionary() {
		let support = FileManager.default.urls(for: .applicationSupportDirectory,
			in: .userDomainMask)[0].appendingPathComponent("HaikuPinyin")
		let file = support.appendingPathComponent("user.dict")
		if FileManager.default.fileExists(atPath: file.path) {
			NSWorkspace.shared.activateFileViewerSelecting([file])
		} else {
			NSWorkspace.shared.open(support)
		}
	}
}

private var settingsWindow: NSWindow?

@_cdecl("HPShowSettings")
public func showSettings() {
	if settingsWindow == nil {
		let window = NSWindow(contentViewController:
			NSHostingController(rootView: SettingsView()))
		window.title = "海酷拼音设置"
		window.styleMask = [.titled, .closable]
		window.isReleasedWhenClosed = false
		window.center()
		settingsWindow = window
	}
	NSApp.activate(ignoringOtherApps: true)
	settingsWindow?.makeKeyAndOrderFront(nil)
}

// #pragma mark - the 中/英 bubble

private var bubble: NSPanel?
private var bubbleLabel: NSTextField?
private var bubbleShown = 0

// Shows 中 or 英 under the line at (x, y), the bottom left of the line in
// screen coordinates, for a moment.
@_cdecl("HPShowModeBubble")
public func showModeBubble(_ chinese: Bool, _ x: Double, _ y: Double) {
	if bubble == nil {
		let panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 30, height: 26),
			styleMask: [.borderless, .nonactivatingPanel], backing: .buffered,
			defer: true)
		panel.level = .popUpMenu
		panel.isOpaque = false
		panel.backgroundColor = .clear
		panel.hasShadow = true
		panel.ignoresMouseEvents = true
		let effect = NSVisualEffectView(frame: panel.contentLayoutRect)
		effect.material = .popover
		effect.state = .active
		effect.wantsLayer = true
		effect.layer?.cornerRadius = 6
		effect.layer?.masksToBounds = true
		let label = NSTextField(labelWithString: "")
		label.font = .systemFont(ofSize: 15, weight: .medium)
		label.alignment = .center
		label.frame = NSRect(x: 0, y: 3, width: 30, height: 20)
		effect.addSubview(label)
		panel.contentView = effect
		bubble = panel
		bubbleLabel = label
	}
	guard let panel = bubble else { return }
	bubbleLabel?.stringValue = chinese ? "中" : "英"
	var origin = NSPoint(x: x, y: y - panel.frame.height - 4)
	if x == 0 && y == 0 {
		// The client didn't say where its text is: by the pointer.
		let mouse = NSEvent.mouseLocation
		origin = NSPoint(x: mouse.x + 8, y: mouse.y - panel.frame.height - 12)
	}
	panel.setFrameOrigin(origin)
	panel.alphaValue = 1
	panel.orderFrontRegardless()
	bubbleShown += 1
	let shown = bubbleShown
	DispatchQueue.main.asyncAfter(deadline: .now() + 0.8) {
		guard shown == bubbleShown else { return }
		NSAnimationContext.runAnimationGroup({ context in
			context.duration = 0.25
			panel.animator().alphaValue = 0
		}, completionHandler: {
			if shown == bubbleShown { panel.orderOut(nil) }
		})
	}
}
