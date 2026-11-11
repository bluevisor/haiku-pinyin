// 海酷拼音 Haiku Pinyin for macOS: the candidate window. A row of numbered
// candidates on glass, the highlighted one in an accent capsule. One inset
// all round: pills sit as far from the panel's edge as text sits from the
// pill's, and the corners are concentric. For VoiceOver each candidate is a
// button, the highlighted one selected, and the highlighted one is announced
// as it changes: the window never takes focus from the app being typed in,
// so VoiceOver wouldn't go there by itself.
import AppKit
import SwiftUI

public typealias HPIndexHandler = @convention(c) (Int32) -> Void

private var pickHandler: HPIndexHandler?		// a click on a candidate
private var pageHandler: HPIndexHandler?		// -1 or +1: arrows, the wheel

// Around the pills, inside the panel.
let candidateInset: CGFloat = 4
// Around the text, inside a pill: the same visual space on every side (a
// 16 pt CJK glyph is about 14 pt tall in a 28 pt pill).
private let pillHeight: CGFloat = 28
private let pillPadding: CGFloat = 8

struct CandidateBar: View {
	var candidates: [String]
	var highlight: Int
	var cloud: Int
	var ai: Int = -1			// the one the AI moved first
	var keys: [Character] = Array("1234567890")	// what picks each
	var hasPrevious: Bool
	var hasNext: Bool
	@State private var hover: Int?

	var body: some View {
		let row = HStack(spacing: 0) {
			ForEach(Array(candidates.enumerated()), id: \.offset) { index, text in
				cell(index, text)
			}
			if hasPrevious || hasNext {
				arrow("chevron.left", enabled: hasPrevious, delta: -1)
				arrow("chevron.right", enabled: hasNext, delta: 1)
			}
		}
		.padding(candidateInset)
		.accessibilityElement(children: .contain)
		.accessibilityLabel(L("Candidates"))
		let shape = Capsule()
		if #available(macOS 26, *) {
			row.glassEffect(.regular, in: shape)
		} else {
			row.background(.regularMaterial, in: shape)
		}
	}

	// What VoiceOver says for a candidate: it, where it is on the page, and
	// the AI's or the cloud's mark.
	func spoken(_ index: Int) -> String {
		var text = L("%@, %ld of %ld", candidates[index], index + 1, candidates.count)
		if index == ai {
			text += L(", AI pick")
		}
		if index == cloud {
			text += L(", from the cloud")
		}
		return text
	}

	private func cell(_ index: Int, _ text: String) -> some View {
		let selected = index == highlight
		let mark = selected ? AnyShapeStyle(.white.opacity(0.75))
			: AnyShapeStyle(.secondary)
		return HStack(alignment: .firstTextBaseline, spacing: 5) {
			Text(index < keys.count ? String(keys[index]) : "")
				.font(.system(size: 12, weight: .medium).monospacedDigit())
				.foregroundStyle(mark)
			Text(text)
				.font(.system(size: 16))
				.foregroundStyle(selected ? AnyShapeStyle(.white)
					: AnyShapeStyle(.primary))
			if index == cloud {
				Image(systemName: "cloud")
					.font(.system(size: 10, weight: .medium))
					.foregroundStyle(mark)
			}
			if index == ai {
				Image(systemName: "sparkles")
					.font(.system(size: 10, weight: .medium))
					.foregroundStyle(mark)
			}
		}
		.fixedSize()	// never truncated: the panel grows instead
		.padding(.horizontal, pillPadding)
		.frame(height: pillHeight)
		.background {
			if selected {
				Capsule().fill(Color.accentColor)
			} else if hover == index {
				Capsule().fill(.primary.opacity(0.08))
			}
		}
		.contentShape(Capsule())
		.onHover { inside in
			if inside {
				hover = index
			} else if hover == index {
				hover = nil
			}
		}
		.onTapGesture { pickHandler?(Int32(index)) }
		.accessibilityElement(children: .ignore)
		.accessibilityLabel(spoken(index))
		.accessibilityAddTraits(selected ? [.isButton, .isSelected] : .isButton)
		.accessibilityAction { pickHandler?(Int32(index)) }
	}

	private func arrow(_ symbol: String, enabled: Bool, delta: Int32) -> some View {
		Image(systemName: symbol)
			.font(.system(size: 11, weight: .semibold))
			.foregroundStyle(enabled ? AnyShapeStyle(.secondary)
				: AnyShapeStyle(.quaternary))
			.frame(width: 20, height: pillHeight)
			.contentShape(Rectangle())
			.onTapGesture {
				if enabled { pageHandler?(delta) }
			}
			.accessibilityLabel(delta < 0 ? L("Previous page") : L("Next page"))
			.accessibilityAddTraits(.isButton)
			.accessibilityAction {
				if enabled { pageHandler?(delta) }
			}
			.accessibilityHidden(!enabled)
	}
}

// Takes clicks without activating, and turns the wheel into pages.
private final class CandidateHostingView: NSHostingView<CandidateBar> {
	private var scrolled: CGFloat = 0

	override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

	override func scrollWheel(with event: NSEvent) {
		scrolled += event.scrollingDeltaY + event.scrollingDeltaX
		let step: CGFloat = event.hasPreciseScrollingDeltas ? 24 : 1
		if abs(scrolled) >= step {
			pageHandler?(scrolled > 0 ? -1 : 1)
			scrolled = 0
		}
	}
}

// Never the key window: keys must go on to the app being typed in.
private final class CandidatePanel: NSPanel {
	override var canBecomeKey: Bool { false }
	override var canBecomeMain: Bool { false }
}

private var panel: NSPanel?
private var hosting: CandidateHostingView?
// Where the page last showed: the line's bottom left, and its height.
private var shownAt = (x: 0.0, y: 0.0, lineHeight: 0.0)

private func makePanel() -> NSPanel {
	let window = CandidatePanel(contentRect: NSRect(x: 0, y: 0, width: 100, height: 36),
		styleMask: [.borderless, .nonactivatingPanel], backing: .buffered,
		defer: true)
	window.level = .popUpMenu
	window.isOpaque = false
	window.backgroundColor = .clear
	window.hasShadow = true
	window.hidesOnDeactivate = false
	let view = CandidateHostingView(rootView: CandidateBar(candidates: [],
		highlight: -1, cloud: -1, hasPrevious: false, hasNext: false))
	window.contentView = view
	hosting = view
	return window
}

@_cdecl("HPSetCandidateHandlers")
public func setCandidateHandlers(_ pick: HPIndexHandler?, _ page: HPIndexHandler?) {
	pickHandler = pick
	pageHandler = page
}

// Shows the page under the text line whose bottom left is (x, y) in screen
// coordinates; above it when there's no room below. candidates are joined
// with newlines; keys, the key that picks each (digits, or letters in
// v-mode).
@_cdecl("HPShowCandidates")
public func showCandidates(_ joined: UnsafePointer<CChar>, _ highlight: Int32,
	_ cloud: Int32, _ ai: Int32, _ hasPrevious: Bool, _ hasNext: Bool,
	_ x: Double, _ y: Double, _ lineHeight: Double,
	_ keys: UnsafePointer<CChar>? = nil) {
	let candidates = String(cString: joined).split(separator: "\n",
		omittingEmptySubsequences: false).map(String.init)
	var bar = CandidateBar(candidates: candidates, highlight: Int(highlight),
		cloud: Int(cloud), ai: Int(ai), hasPrevious: hasPrevious, hasNext: hasNext)
	if let keys {
		bar.keys = Array(String(cString: keys))
	}
	show(bar, x, y, lineHeight)
}

private func show(_ bar: CandidateBar, _ x: Double, _ y: Double,
	_ lineHeight: Double) {
	let window = panel ?? makePanel()
	panel = window
	guard let view = hosting else { return }
	view.rootView = bar
	let size = view.fittingSize
	window.setContentSize(size)

	var anchor = NSPoint(x: x, y: y)
	if x == 0 && y == 0 {
		anchor = NSEvent.mouseLocation
	}
	let screen = NSScreen.screens.first { $0.frame.contains(anchor) }
		?? NSScreen.main
	let visible = screen?.visibleFrame ?? .infinite
	// The first candidate's text under the first letter typed.
	var origin = NSPoint(x: anchor.x - candidateInset - pillPadding,
		y: anchor.y - 4 - size.height)
	if origin.y < visible.minY {
		origin.y = anchor.y + max(lineHeight, 16) + 4
	}
	origin.x = min(max(origin.x, visible.minX), visible.maxX - size.width)
	window.setFrameOrigin(origin)
	window.orderFrontRegardless()
	shownAt = (anchor.x, anchor.y, lineHeight)
	invitationTyping()
	announce(bar)
}

// What VoiceOver was last told, so it hears each change once.
private(set) var announced = ""
// Tests turn this on without VoiceOver.
var announceAlways = false

private func announce(_ bar: CandidateBar) {
	guard announceAlways || NSWorkspace.shared.isVoiceOverEnabled,
		bar.candidates.indices.contains(bar.highlight) else { return }
	let text = bar.spoken(bar.highlight)
	guard text != announced else { return }
	announced = text
	// High: a newer one cuts off the last, as the highlight moves on.
	NSAccessibility.post(element: NSApp as Any, notification: .announcementRequested,
		userInfo: [.announcement: text,
			.priority: NSAccessibilityPriorityLevel.high.rawValue])
}

@_cdecl("HPHideCandidates")
public func hideCandidates() {
	guard let panel, panel.isVisible else { return }
	panel.orderOut(nil)
	announced = ""
	invitationWordEnded(shownAt.x, shownAt.y, shownAt.lineHeight)
}

// #pragma mark - the AI's completion

// Gray text drawn just after the cursor, in the client's font size: marked
// text can't be gray (clients draw it their own way, underlined in blue), so
// the completion floats over the text instead, and is never in it until Tab.
private var ghostPanel: NSPanel?
private var ghostLabel: NSTextField?

private func makeGhost() -> NSPanel {
	let window = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 10, height: 10),
		styleMask: [.borderless, .nonactivatingPanel], backing: .buffered,
		defer: true)
	window.level = .popUpMenu
	window.isOpaque = false
	window.backgroundColor = .clear
	window.hasShadow = false
	window.hidesOnDeactivate = false
	window.ignoresMouseEvents = true
	let label = NSTextField(labelWithString: "")
	label.textColor = .tertiaryLabelColor
	label.lineBreakMode = .byClipping
	window.contentView = label
	ghostLabel = label
	return window
}

// The line the cursor is on: its bottom left (x, y) in screen coordinates
// and height. font: the client's, or "" for the system font.
@_cdecl("HPShowGhost")
public func showGhost(_ text: UnsafePointer<CChar>, _ x: Double, _ y: Double,
	_ lineHeight: Double, _ fontName: UnsafePointer<CChar>, _ fontSize: Double) {
	let window = ghostPanel ?? makeGhost()
	ghostPanel = window
	guard let label = ghostLabel else { return }
	let size = fontSize > 0 ? fontSize : max(lineHeight * 0.75, 12)
	let name = String(cString: fontName)
	label.font = (name.isEmpty ? nil : NSFont(name: name, size: size))
		?? NSFont.systemFont(ofSize: size)
	label.stringValue = String(cString: text)
	let fitting = label.fittingSize
	// Centred on the line, as the line's own text sits in it.
	// A label draws its text 2 pt in from its edge.
	window.setFrame(NSRect(x: x - 2, y: y + (lineHeight - fitting.height) / 2,
		width: fitting.width, height: fitting.height), display: true)
	window.orderFrontRegardless()
}

@_cdecl("HPHideGhost")
public func hideGhost() {
	ghostPanel?.orderOut(nil)
}
