// 海酷拼音 Haiku Pinyin for macOS: the AI's one invitation. On an Apple
// silicon Mac with no model, the AI is easy never to find. So once, after a
// word has gone in and the keyboard has been still for a moment, a small
// card under the line offers it: Set Up… opens the settings at the AI
// section, with the download. The card never takes focus (a non-activating
// panel that can't become key), goes as soon as a key is pressed, and leaves
// by itself after a while. Once shown it's never shown again (aiInvited).
import AppKit
import SwiftUI

private let invitedKey = "aiInvited"
private let quietBefore: TimeInterval = 2		// seconds with no key down
private let shownFor: TimeInterval = 20

struct InvitationCard: View {
	var setUp: () -> Void
	var dismiss: () -> Void

	var body: some View {
		let card = VStack(alignment: .leading, spacing: 10) {
			HStack(alignment: .top, spacing: 10) {
				Image(systemName: "sparkles")
					.font(.system(size: 13, weight: .semibold))
					.foregroundStyle(.white)
					.frame(width: 26, height: 26)
					.background(Color.purple.gradient, in: RoundedRectangle(
						cornerRadius: 7, style: .continuous))
				VStack(alignment: .leading, spacing: 3) {
					Text(L("Try on-device AI"))
						.font(.system(size: 13, weight: .semibold))
					Text(L("Better word picks and a guess at what comes next, all on this Mac. A 1.4 GB download."))
						.font(.system(size: 12))
						.foregroundStyle(.secondary)
						.fixedSize(horizontal: false, vertical: true)
				}
			}
			HStack(spacing: 8) {
				Spacer()
				Button(L("Not Now"), action: dismiss)
					.controlSize(.small)
				Button(L("Set Up…"), action: setUp)
					.controlSize(.small)
					.buttonStyle(.borderedProminent)
			}
		}
		.padding(12)
		.frame(width: 300)
		let shape = RoundedRectangle(cornerRadius: 14, style: .continuous)
		if #available(macOS 26, *) {
			card.glassEffect(.regular, in: shape)
		} else {
			card.background(.regularMaterial, in: shape)
		}
	}
}

// Takes clicks without activating; never the key window, so typing stays
// in the app being typed in.
private final class InvitationPanel: NSPanel {
	override var canBecomeKey: Bool { false }
	override var canBecomeMain: Bool { false }
}

private final class InvitationHostingView: NSHostingView<InvitationCard> {
	override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }
}

private var invitationPanel: NSPanel?
private var waiting = 0		// bumped when typing starts: a pending card is off
private var watch: Timer?

private var keyIdle: TimeInterval {
	CGEventSource.secondsSinceLastEventType(.combinedSessionState, eventType: .keyDown)
}

// Whether there's anything to invite to.
private var invitationWanted: Bool {
	let defaults = UserDefaults.standard
	return !defaults.bool(forKey: invitedKey) && aiSupported
		&& installedModels().isEmpty && !ModelDownloader.shared.running
		// Turned off on purpose (both off is a choice, not a default).
		&& (defaults.object(forKey: "aiPicks") as? Bool ?? true
			|| defaults.object(forKey: "aiComplete") as? Bool ?? true)
}

// Candidates.swift: a page of candidates is showing; any card goes.
func invitationTyping() {
	waiting += 1
	hideInvitation()
}

// Candidates.swift: the page closed (a word went in, or was dropped) under
// the line at (x, y). If the keyboard then rests, the card shows there.
func invitationWordEnded(_ x: Double, _ y: Double, _ lineHeight: Double) {
	guard invitationPanel?.isVisible != true, invitationWanted else { return }
	waiting += 1
	let ticket = waiting
	let app = NSWorkspace.shared.frontmostApplication?.processIdentifier
	DispatchQueue.main.asyncAfter(deadline: .now() + quietBefore) {
		// Typing on, another app, a mouse down: the next word tries again.
		guard ticket == waiting, keyIdle >= quietBefore - 0.2,
			NSEvent.pressedMouseButtons == 0,
			NSWorkspace.shared.frontmostApplication?.processIdentifier == app,
			invitationWanted else { return }
		showInvitation(x, y, lineHeight)
	}
}

func showInvitation(_ x: Double, _ y: Double, _ lineHeight: Double) {
	UserDefaults.standard.set(true, forKey: invitedKey)
	let panel = invitationPanel ?? makeInvitation()
	invitationPanel = panel
	let size = panel.contentView?.fittingSize ?? NSSize(width: 300, height: 100)
	panel.setContentSize(size)
	let anchor = NSPoint(x: x, y: y)
	let screen = NSScreen.screens.first { $0.frame.contains(anchor) } ?? NSScreen.main
	let visible = screen?.visibleFrame ?? .infinite
	var origin = NSPoint(x: x - 12, y: y - 6 - size.height)
	if origin.y < visible.minY {
		origin.y = y + max(lineHeight, 16) + 6
	}
	origin.x = min(max(origin.x, visible.minX), visible.maxX - size.width)
	panel.setFrameOrigin(origin)
	panel.alphaValue = 1
	panel.orderFrontRegardless()

	// Gone at the next key, or after a while.
	let shown = Date()
	watch?.invalidate()
	watch = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { _ in
		if keyIdle < -shown.timeIntervalSinceNow || -shown.timeIntervalSinceNow > shownFor {
			hideInvitation()
		}
	}
}

func hideInvitation() {
	watch?.invalidate()
	watch = nil
	guard let panel = invitationPanel, panel.isVisible else { return }
	NSAnimationContext.runAnimationGroup({ context in
		context.duration = 0.2
		panel.animator().alphaValue = 0
	}, completionHandler: {
		if watch == nil { panel.orderOut(nil) }
	})
}

private func makeInvitation() -> NSPanel {
	let panel = InvitationPanel(contentRect: NSRect(x: 0, y: 0, width: 300, height: 100),
		styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: true)
	panel.level = .popUpMenu
	panel.isOpaque = false
	panel.backgroundColor = .clear
	panel.hasShadow = true
	panel.hidesOnDeactivate = false
	panel.contentView = InvitationHostingView(rootView: InvitationCard(
		setUp: {
			hideInvitation()
			showSettings(at: SettingsTarget.ai)
		},
		dismiss: { hideInvitation() }))
	return panel
}
