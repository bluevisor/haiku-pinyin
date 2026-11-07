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

// A System Settings row icon: a symbol on a colored tile.
private struct Tile: View {
	let symbol: String
	let color: Color

	var body: some View {
		Image(systemName: symbol)
			.font(.system(size: 11, weight: .semibold))
			.foregroundStyle(.white)
			.frame(width: 20, height: 20)
			.background(color.gradient, in: RoundedRectangle(cornerRadius: 5,
				style: .continuous))
	}
}

private struct Row: View {
	let title: String
	let symbol: String
	let color: Color

	var body: some View {
		HStack(spacing: 8) {
			Tile(symbol: symbol, color: color)
			Text(title)
		}
	}
}

// The method's icon: 中 on blue glass.
private struct Badge: View {
	var body: some View {
		let shape = RoundedRectangle(cornerRadius: 12, style: .continuous)
		let text = Text("中")
			.font(.system(size: 27, weight: .semibold))
			.foregroundStyle(.white)
			.frame(width: 52, height: 52)
		if #available(macOS 26, *) {
			text
				.background(Color.accentColor.gradient, in: shape)
				.glassEffect(.regular.tint(.accentColor.opacity(0.35)), in: shape)
		} else {
			text.background(Color.accentColor.gradient, in: shape)
		}
	}
}

private extension View {
	// Glass buttons from macOS 26 on.
	@ViewBuilder func glassButton() -> some View {
		if #available(macOS 26, *) {
			buttonStyle(.glass)
		} else {
			self
		}
	}
}

struct SettingsView: View {
	@AppStorage("layout") private var layout = "full"
	@AppStorage("script") private var script = 0
	@AppStorage("pageSize") private var pageSize = 7
	@AppStorage("fullwidth") private var fullwidth = true
	@AppStorage("emoji") private var emoji = true
	@AppStorage("english") private var english = true
	@AppStorage("cloud") private var cloud = false
	@AppStorage("fuzzy") private var fuzzy = 0
	@AppStorage("vowels") private var vowels = 0	// engine's Vowels
	@AppStorage("aiPicks") private var aiPicks = true
	@AppStorage("aiComplete") private var aiComplete = true
	@AppStorage("aiModel") private var aiModel = ""		// "": 自动
	@State private var models = installedModels()

	private var aiInstalled: Bool {
		!models.isEmpty && Bundle.main.path(forAuxiliaryExecutable: "HaikuPinyinAI") != nil
	}

	private var version: String {
		Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString")
			as? String ?? ""
	}

	private func fuzzyGroup(_ mask: Int) -> Binding<Bool> {
		Binding(get: { fuzzy & mask == mask },
			set: { fuzzy = $0 ? fuzzy | mask : fuzzy & ~mask })
	}

	var body: some View {
		Form {
			Section {
				HStack(spacing: 14) {
					Badge()
					VStack(alignment: .leading, spacing: 3) {
						Text("海酷拼音")
							.font(.title2.weight(.semibold))
						Text("全拼与双拼，整句输入")
							.foregroundStyle(.secondary)
					}
					Spacer()
					Text(version)
						.font(.callout.monospacedDigit())
						.foregroundStyle(.tertiary)
				}
				.padding(.vertical, 4)
			}

			Section("输入") {
				Picker(selection: $layout) {
					ForEach(layouts, id: \.id) { Text($0.name).tag($0.id) }
				} label: {
					Row(title: "拼写方式", symbol: "keyboard", color: .blue)
				}
				if layout != "full" {
					Picker(selection: $vowels) {
						Text("aa  oo  ee").tag(0)
						Text("oa  oo  oe").tag(1)
						Text("a  o  e（单键）").tag(2)
					} label: {
						Row(title: "双拼 a o e", symbol: "character", color: .indigo)
					}
				}
				Picker(selection: $script) {
					Text("简体中文").tag(0)
					Text("繁體中文").tag(1)
				} label: {
					Row(title: "字形", symbol: "character.book.closed", color: .orange)
				}
				Picker(selection: $pageSize) {
					ForEach([5, 7, 9], id: \.self) { Text("\($0) 个").tag($0) }
				} label: {
					Row(title: "每页候选", symbol: "list.number", color: .gray)
				}
			}

			Section {
				Toggle(isOn: $english) {
					Row(title: "中英混输", symbol: "textformat.abc", color: .teal)
				}
				Toggle(isOn: $emoji) {
					Row(title: "Emoji 候选", symbol: "face.smiling", color: .yellow)
				}
				Toggle(isOn: $fullwidth) {
					Row(title: "全角标点", symbol: "quote.opening", color: .pink)
				}
			} header: {
				Text("候选")
			} footer: {
				Text("中英混输：hello 也给出 hello。")
					.foregroundStyle(.secondary)
			}

			Section {
				Toggle(isOn: $aiPicks) {
					Row(title: "AI 选词", symbol: "sparkles", color: .purple)
				}
				Toggle(isOn: $aiComplete) {
					Row(title: "AI 补全 (Tab)", symbol: "text.append", color: .green)
				}
				Picker(selection: $aiModel) {
					Text("自动").tag("")
					ForEach(models, id: \.name) { model in
						Text("\(model.name)  \(model.size)").tag(model.name)
					}
					// A chosen model that's gone: shown, so the choice is clear.
					if !aiModel.isEmpty && !models.contains(where: { $0.name == aiModel }) {
						Text("\(aiModel)（未找到）").tag(aiModel)
					}
				} label: {
					Row(title: "模型", symbol: "cpu", color: .gray)
				}
				LabeledContent {
					Button("在访达中显示") { revealModels() }
						.glassButton()
				} label: {
					Row(title: "模型文件夹", symbol: "folder.fill", color: .blue)
				}
			} header: {
				Text("AI")
			} footer: {
				Text(aiInstalled
					? "在本机运行的语言模型读光标前的文字：把更通顺的候选排到第一（标 ✦），上屏后给出接下来的字，按 Tab 输入。文字不离开这台 Mac。自动：用 Qwen3-1.7B，这台 Mac 跑得慢时换小的。模型文件夹里的 MLX 模型都可以选。"
					: "未安装模型：运行 macos/ai/fetch-models.sh，或把 MLX 模型放进模型文件夹。需要 Apple 芯片。")
					.foregroundStyle(.secondary)
			}
			.disabled(!aiInstalled && models.isEmpty)
			.onAppear { models = installedModels() }

			Section("模糊音") {
				ForEach(fuzzyGroups, id: \.mask) {
					Toggle($0.name, isOn: fuzzyGroup($0.mask))
				}
			}

			Section {
				Toggle(isOn: $cloud) {
					Row(title: "云候选 (Google)", symbol: "cloud.fill", color: .cyan)
				}
			} footer: {
				Text("把正在输入的拼音发给 Google 输入工具，不含已上屏的文字。")
					.foregroundStyle(.secondary)
			}

			Section {
				LabeledContent {
					Button("在访达中显示") { revealUserDictionary() }
						.glassButton()
				} label: {
					Row(title: "学到的词", symbol: "book.closed.fill", color: .brown)
				}
			} footer: {
				Text("轻按 Shift 切换中 / 英；在候选上按 Control-Delete 忘掉学到的词。")
					.foregroundStyle(.secondary)
			}
		}
		.formStyle(.grouped)
		// Scrolls under the clear title bar when the screen is short.
		.frame(width: 480)
		.frame(minHeight: 360, idealHeight: 720)
	}

	private func revealModels() {
		let folder = modelsFolder()
		try? FileManager.default.createDirectory(at: folder,
			withIntermediateDirectories: true)
		NSWorkspace.shared.open(folder)
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

// ~/Library/Application Support/HaikuPinyin/models, as AI.mm has it.
private func modelsFolder() -> URL {
	FileManager.default.urls(for: .applicationSupportDirectory,
		in: .userDomainMask)[0].appendingPathComponent("HaikuPinyin/models")
}

struct InstalledModel {
	let name: String
	let size: String		// of its weights
}

// The MLX models there: folders (or links to them) with a config.json.
private func installedModels() -> [InstalledModel] {
	let manager = FileManager.default
	let folder = modelsFolder()
	let names = (try? manager.contentsOfDirectory(atPath: folder.path)) ?? []
	return names.sorted().compactMap { name in
		let directory = folder.appendingPathComponent(name).resolvingSymlinksInPath()
		guard manager.fileExists(atPath: directory.appendingPathComponent("config.json").path)
		else { return nil }
		let files = (try? manager.contentsOfDirectory(atPath: directory.path)) ?? []
		let bytes = files.filter { $0.hasSuffix(".safetensors") }.reduce(Int64(0)) {
			let size = (try? manager.attributesOfItem(atPath:
				directory.appendingPathComponent($1).path)[.size]) as? Int64
			return $0 + (size ?? 0)
		}
		return InstalledModel(name: name, size: ByteCountFormatter.string(
			fromByteCount: bytes, countStyle: .file))
	}
}

private var settingsWindow: NSWindow?

@_cdecl("HPShowSettings")
public func showSettings() {
	if settingsWindow == nil {
		let window = NSWindow(contentViewController:
			NSHostingController(rootView: SettingsView()))
		window.title = "海酷拼音"
		// The form runs under a clear title bar, as System Settings does.
		window.styleMask = [.titled, .closable, .resizable,
			.fullSizeContentView]
		window.titlebarAppearsTransparent = true
		window.setContentSize(NSSize(width: 480, height: min(720,
			(NSScreen.main?.visibleFrame.height ?? 800) - 80)))
		window.isReleasedWhenClosed = false
		window.center()
		settingsWindow = window
	}
	NSApp.activate(ignoringOtherApps: true)
	settingsWindow?.makeKeyAndOrderFront(nil)
}

// #pragma mark - the 中/英 bubble

// A circle, so the glyph has the same space on every side.
private let bubbleSize: CGFloat = 30

struct ModeBubble: View {
	var chinese: Bool

	var body: some View {
		let text = Text(chinese ? "中" : "英")
			.font(.system(size: 16, weight: .semibold))
			.frame(width: bubbleSize, height: bubbleSize)
		if #available(macOS 26, *) {
			text.glassEffect(.regular, in: Circle())
		} else {
			text.background(.regularMaterial, in: Circle())
		}
	}
}

private var bubble: NSPanel?
private var bubbleView: NSHostingView<ModeBubble>?
private var bubbleShown = 0

private func makeBubble() -> NSPanel {
	let panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: bubbleSize,
			height: bubbleSize),
		styleMask: [.borderless, .nonactivatingPanel], backing: .buffered,
		defer: true)
	panel.level = .popUpMenu
	panel.isOpaque = false
	panel.backgroundColor = .clear
	panel.hasShadow = true
	panel.ignoresMouseEvents = true
	let view = NSHostingView(rootView: ModeBubble(chinese: true))
	panel.contentView = view
	bubbleView = view
	return panel
}

// Shows 中 or 英 under the line at (x, y), the bottom left of the line in
// screen coordinates, for a moment.
@_cdecl("HPShowModeBubble")
public func showModeBubble(_ chinese: Bool, _ x: Double, _ y: Double) {
	let panel = bubble ?? makeBubble()
	bubble = panel
	bubbleView?.rootView = ModeBubble(chinese: chinese)
	var origin = NSPoint(x: x, y: y - bubbleSize - 4)
	if x == 0 && y == 0 {
		// The client didn't say where its text is: by the pointer.
		let mouse = NSEvent.mouseLocation
		origin = NSPoint(x: mouse.x + 8, y: mouse.y - bubbleSize - 12)
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
