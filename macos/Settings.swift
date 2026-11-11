// 海酷拼音 Haiku Pinyin for macOS: the settings window, and the 中/英 bubble
// a Shift tap shows. Settings live in the method's user defaults; the input
// controller applies them as they change.
import AppKit
import SwiftUI

private let layouts: [(id: String, name: String)] = [
	("full", L("Full pinyin")),
	("pyjj", L("Shuangpin · Pinyin Jiajia")),
	("xiaohe", L("Shuangpin · Xiaohe")),
	("ziranma", L("Shuangpin · Ziranma")),
	("mspy", L("Shuangpin · Microsoft")),
	("abc", L("Shuangpin · Intelligent ABC")),
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
	let title: String		// already in the system's language (L)
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
	@AppStorage("autoUpdate") private var autoUpdate = true
	@State private var models = installedModels()
	@ObservedObject private var downloader = ModelDownloader.shared
	@ObservedObject private var updater = Updater.shared
	@State private var aiState = 0		// bumped on HPAIStateChanged, to redraw

	private var aiInstalled: Bool { !models.isEmpty && aiSupported }
	// The model the settings pick is there and didn't fail to load. Until it
	// is, the AI toggles show off: the AI isn't doing anything.
	private var aiUsable: Bool { _ = aiState; return aiInstalled && aiUsableNow() }
	private var aiFailed: Bool { aiInstalled && aiFailedNow() }

	// A setting as it works now: off while the AI can't run, whatever's saved.
	private func aiSetting(_ saved: Binding<Bool>) -> Binding<Bool> {
		Binding(get: { saved.wrappedValue && aiUsable },
			set: { saved.wrappedValue = $0 })
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
		// Opened to a section (showSettings(at:)): scrolled to it.
		ScrollViewReader { scroll in
			form.onReceive(SettingsTarget.shared.$section) { section in
				guard let section else { return }
				DispatchQueue.main.async {
					scroll.scrollTo(section, anchor: .top)
					SettingsTarget.shared.section = nil
				}
			}
		}
		// Scrolls when the screen is short.
		.frame(width: 480)
		.frame(minHeight: 360, idealHeight: 720)
	}

	private var form: some View {
		Form {
			Section {
				HStack(spacing: 14) {
					Badge()
					VStack(alignment: .leading, spacing: 3) {
						Text(L("Haiku Pinyin"))
							.font(.title2.weight(.semibold))
						Text(L("Whole-sentence pinyin with on-device AI"))
							.foregroundStyle(.secondary)
					}
					Spacer()
					Text(version)
						.font(.callout.monospacedDigit())
						.foregroundStyle(.tertiary)
				}
				.padding(.vertical, 4)
			}

			Section(L("Input")) {
				Picker(selection: $layout) {
					ForEach(layouts, id: \.id) { Text($0.name).tag($0.id) }
				} label: {
					Row(title: L("Typing scheme"), symbol: "keyboard", color: .blue)
				}
				if layout != "full" {
					Picker(selection: $vowels) {
						Text(verbatim: "aa  oo  ee").tag(0)
						Text(verbatim: "oa  oo  oe").tag(1)
						Text(L("a  o  e (one key)")).tag(2)
					} label: {
						Row(title: L("Shuangpin a o e"), symbol: "character", color: .indigo)
					}
				}
				Picker(selection: $script) {
					Text(L("Simplified Chinese")).tag(0)
					Text(L("Traditional Chinese")).tag(1)
				} label: {
					Row(title: L("Characters"), symbol: "character.book.closed",
						color: .orange)
				}
				Picker(selection: $pageSize) {
					ForEach([5, 7, 9], id: \.self) { Text(L("%ld candidates", $0)).tag($0) }
				} label: {
					Row(title: L("Per page"), symbol: "list.number", color: .gray)
				}
			}

			Section {
				Toggle(isOn: $english) {
					Row(title: L("Mixed Chinese and English"), symbol: "textformat.abc",
						color: .teal)
						// Abc, as it's about English: in Chinese the symbol
						// is 甲乙丙, too wide for its tile.
						.environment(\.locale, Locale(identifier: "en"))
				}
				Toggle(isOn: $emoji) {
					Row(title: L("Emoji"), symbol: "face.smiling", color: .yellow)
				}
				Toggle(isOn: $fullwidth) {
					Row(title: L("Full-width punctuation"), symbol: "quote.opening",
						color: .pink)
				}
			} header: {
				Text(L("Candidates"))
			} footer: {
				Text(L("Mixed Chinese and English: typing hello also offers hello."))
					.foregroundStyle(.secondary)
			}

			Section {
				Group {
					Toggle(isOn: aiSetting($aiPicks)) {
						Row(title: L("AI picks"), symbol: "sparkles", color: .purple)
					}
					Toggle(isOn: aiSetting($aiComplete)) {
						Row(title: L("AI completion (Tab)"), symbol: "text.append",
							color: .green)
					}
				}
				.disabled(!aiUsable)
				Group {
					Picker(selection: $aiModel) {
						Text(L("Automatic")).tag("")
						ForEach(models, id: \.name) { model in
							Text(verbatim: "\(model.name)  \(model.size)").tag(model.name)
						}
						// A chosen model that's gone: shown, so the choice is clear.
						if !aiModel.isEmpty && !models.contains(where: { $0.name == aiModel }) {
							Text(L("%@ (not found)", aiModel)).tag(aiModel)
						}
					} label: {
						Row(title: L("Model"), symbol: "cpu", color: .gray)
					}
				}
				.disabled(!aiInstalled)
				if aiSupported && (downloader.running || !downloader.missing.isEmpty) {
					modelDownload
				}
				LabeledContent {
					Button(L("Show in Finder")) { revealModels() }
						.glassButton()
				} label: {
					Row(title: L("Models folder"), symbol: "folder.fill", color: .blue)
				}
			} header: {
				Text("AI")
			} footer: {
				Text(aiFooter)
					.foregroundStyle(.secondary)
			}
			.onAppear { models = installedModels() }
			.onChange(of: downloader.running) { _ in models = installedModels() }
			.onReceive(NotificationCenter.default.publisher(for:
				Notification.Name("HPAIStateChanged")).receive(on: RunLoop.main)) { _ in
				aiState += 1
			}
			.id(SettingsTarget.ai)

			Section(L("Fuzzy pinyin")) {
				ForEach(fuzzyGroups, id: \.mask) {
					Toggle($0.name, isOn: fuzzyGroup($0.mask))
				}
			}

			Section {
				Toggle(isOn: $cloud) {
					Row(title: L("Cloud candidates (Google)"), symbol: "cloud.fill",
						color: .cyan)
				}
			} footer: {
				Text(L("Sends the pinyin being typed to Google Input Tools, never the text already entered."))
					.foregroundStyle(.secondary)
			}

			Section {
				LabeledContent {
					Button(L("Show in Finder")) { revealUserDictionary() }
						.glassButton()
				} label: {
					Row(title: L("Learned words"), symbol: "book.closed.fill", color: .brown)
				}
			} footer: {
				Text(L("Tap Shift to switch between Chinese and English. Control-Delete on a candidate forgets a learned word."))
					.foregroundStyle(.secondary)
			}

			Section {
				Toggle(isOn: $autoUpdate) {
					Row(title: L("Update automatically"), symbol: "arrow.triangle.2.circlepath",
						color: .green)
				}
				LabeledContent {
					if updater.staged != nil {
						Button(L("Install Now")) { updater.installNow() }
							.glassButton()
					} else {
						Button(L("Check for Updates…")) { updater.check(manual: true) }
							.glassButton()
							.disabled(updater.busy)
					}
				} label: {
					Row(title: L("Version %@", version), symbol: "info.circle.fill",
						color: .gray)
				}
			} header: {
				Text(L("Updates"))
			} footer: {
				Text(updateFooter)
					.foregroundStyle(.secondary)
			}
		}
		.formStyle(.grouped)
	}

	// The models missing, a button to fetch them, and how far it's got.
	@ViewBuilder private var modelDownload: some View {
		let size = ByteCountFormatter.string(fromByteCount:
			downloader.missing.reduce(0) { $0 + $1.size }, countStyle: .file)
		if downloader.running {
			VStack(alignment: .leading, spacing: 6) {
				HStack {
					Row(title: L("Downloading models"), symbol: "arrow.down.circle.fill",
						color: .blue)
					Spacer()
					Button(L("Cancel")) { downloader.cancel() }
						.glassButton()
				}
				ProgressView(value: Double(downloader.done),
					total: Double(max(downloader.total, 1)))
				Text(verbatim: "\(ByteCountFormatter.string(fromByteCount: downloader.done, countStyle: .file)) / \(ByteCountFormatter.string(fromByteCount: downloader.total, countStyle: .file))")
					.font(.caption.monospacedDigit())
					.foregroundStyle(.secondary)
			}
		} else {
			LabeledContent {
				Button(L("Download")) { downloader.start() }
					.glassButton()
			} label: {
				VStack(alignment: .leading, spacing: 2) {
					Row(title: models.isEmpty ? L("Download the AI models")
							: L("Download the other models"),
						symbol: "arrow.down.circle.fill", color: .blue)
					Text(downloader.message.isEmpty ? size
						: L("%@ · %@ to go", downloader.message, size))
						.font(.caption)
						.foregroundStyle(.secondary)
				}
			}
		}
	}

	private var aiFooter: String {
		if !aiSupported {
			return L("AI needs a Mac with Apple silicon.")
		}
		if aiFailed {
			return L("The model didn't load, so AI is off. Choose another model, or delete it from the models folder and download it again.")
		}
		if aiInstalled && !aiUsable {
			return L("The chosen model isn't there, so AI is off. Choose a model you have, or download the default ones.")
		}
		if aiInstalled {
			return L("A language model on this Mac reads the text before the cursor. It moves the candidate that reads best to the front (marked ✦), and after you enter a word it shows what likely comes next; Tab takes it. Nothing you type leaves this Mac. Automatic uses Qwen3-1.7B, and a smaller model on a Mac too slow for it. Any MLX model in the models folder can be chosen.")
		}
		return L("Download the models first: Qwen3's 1.7B and 0.8B base models, about 1.4 GB in all, from Hugging Face. They take about 1 GB of memory while AI is on. Nothing you type leaves this Mac.")
	}

	// Sentences, each with its own stop: a space between them in English,
	// none in Chinese.
	private var updateFooter: String {
		var parts: [String] = []
		if !updater.status.isEmpty {
			parts.append(updater.status)
		}
		if let last = updater.lastCheck {
			parts.append(L("Last checked %@.",
				last.formatted(date: .abbreviated, time: .shortened)))
		}
		parts.append(L("Updates come from the releases on GitHub; each is checked against its signature and goes in while the Mac is idle."))
		return parts.joined(separator: L(" "))
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

struct InstalledModel {
	let name: String
	let size: String		// of its weights
}

// The MLX models there: folders (or links to them) with a config.json.
func installedModels() -> [InstalledModel] {
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

// The section the window is to show when it opens.
final class SettingsTarget: ObservableObject {
	static let shared = SettingsTarget()
	static let ai = "ai"
	@Published var section: String?
}

// A solid title bar: the form scrolls under it, and a clear one let the
// rows show through behind the title and the window buttons.
func styleSettingsWindow(_ window: NSWindow) {
	window.title = L("Haiku Pinyin")
	window.styleMask = [.titled, .closable, .resizable]
	window.titlebarAppearsTransparent = false
}

@_cdecl("HPShowSettings")
public func showSettings() {
	showSettings(at: nil)
}

func showSettings(at section: String?) {
	SettingsTarget.shared.section = section
	if settingsWindow == nil {
		let window = NSWindow(contentViewController:
			NSHostingController(rootView: SettingsView()))
		styleSettingsWindow(window)
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
		let label = chinese ? "中" : L("EN")
		// Two letters fit the circle a size down.
		let text = Text(label)
			.font(.system(size: label.count > 1 ? 13 : 16, weight: .semibold))
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
