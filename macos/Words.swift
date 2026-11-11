// 海酷拼音 Haiku Pinyin for macOS: the settings window's word lists section
// (the custom phrases file, and importing and exporting learned words) and
// its sync section (Words.mm does the work).
import AppKit
import SwiftUI
import UniformTypeIdentifiers

// Words.mm: the phrases file (made if missing); an import, answering with
// the format it read and counts; an export, answering with the count or -1.
@_silgen_name("HPPhrasesFile")
private func phrasesFile() -> UnsafePointer<CChar>
@_silgen_name("HPImportWords")
private func importWords(_ path: UnsafePointer<CChar>,
	_ words: UnsafeMutablePointer<Int32>, _ phrases: UnsafeMutablePointer<Int32>,
	_ skipped: UnsafeMutablePointer<Int32>, _ problem: UnsafeMutablePointer<Int32>)
	-> UnsafePointer<CChar>
@_silgen_name("HPExportWords")
private func exportWords(_ path: UnsafePointer<CChar>) -> Int32
// The sync folder ("" when off), and setting it ("" turns it off).
@_silgen_name("HPSyncFolder")
private func syncFolder() -> UnsafePointer<CChar>
@_silgen_name("HPSetSyncFolder")
private func setSyncFolder(_ folder: UnsafePointer<CChar>) -> Bool

// Sync through a folder the user's own tool syncs (engine/Sync.h): the
// folder, chosen with an open panel, and what it means for privacy.
struct SyncSection: View {
	@State private var folder = String(cString: syncFolder())
	@State private var failed = false

	var body: some View {
		Section {
			LabeledContent {
				HStack {
					if !folder.isEmpty {
						Button(L("Turn Off")) { set("") }
							.glassButton()
					}
					Button(L("Choose…")) { choose() }
						.glassButton()
				}
			} label: {
				VStack(alignment: .leading, spacing: 2) {
					Row(title: L("Sync folder"), symbol: "arrow.triangle.2.circlepath.icloud.fill",
						color: .blue)
					Text(folder.isEmpty ? L("Off") : shortened(folder))
						.font(.caption)
						.foregroundStyle(.secondary)
						.lineLimit(1)
						.truncationMode(.middle)
						.padding(.leading, 28)
				}
			}
		} header: {
			Text(L("Sync"))
		} footer: {
			Text(failed ? L("Couldn't save the sync folder.")
				: L("Pick a folder your own sync tool syncs (iCloud Drive, Dropbox, Syncthing, Nextcloud, a network share) to keep learned words and custom phrases alike on your Macs and PCs. Each one writes only its own files there. The folder holds the words it learned: anyone who can see it can read them. Haiku Pinyin itself sends nothing anywhere."))
				.foregroundStyle(.secondary)
		}
	}

	private func choose() {
		let panel = NSOpenPanel()
		panel.title = L("Choose a Sync Folder")
		panel.message = L("A folder your sync tool keeps on each computer, such as one in iCloud Drive or Dropbox")
		panel.prompt = L("Choose")
		panel.canChooseFiles = false
		panel.canChooseDirectories = true
		panel.canCreateDirectories = true
		panel.allowsMultipleSelection = false
		if !folder.isEmpty {
			panel.directoryURL = URL(fileURLWithPath: folder)
		}
		NSApp.activate(ignoringOtherApps: true)
		panel.begin { response in
			guard response == .OK, let url = panel.url else { return }
			set(url.path)
		}
	}

	private func set(_ path: String) {
		failed = !setSyncFolder(path)
		folder = String(cString: syncFolder())
	}
}

// ~ for the home folder, and iCloud Drive by its name.
private func shortened(_ path: String) -> String {
	let home = NSHomeDirectory()
	let iCloud = home + "/Library/Mobile Documents/com~apple~CloudDocs"
	if path == iCloud || path.hasPrefix(iCloud + "/") {
		return L("iCloud Drive") + path.dropFirst(iCloud.count)
	}
	return path.hasPrefix(home + "/") ? "~" + path.dropFirst(home.count) : path
}

struct WordsSection: View {
	@State private var status = ""
	@State private var busy = false

	var body: some View {
		Section {
			LabeledContent {
				Button(L("Edit…")) { openPhrases() }
					.glassButton()
			} label: {
				Row(title: L("Custom phrases"), symbol: "text.bubble.fill", color: .mint)
			}
			LabeledContent {
				HStack {
					Button(L("Import…")) { importFile() }
						.glassButton()
					Button(L("Export…")) { exportFile() }
						.glassButton()
				}
				.disabled(busy)
			} label: {
				Row(title: L("Import and export words"),
					symbol: "arrow.up.arrow.down.square.fill", color: .indigo)
			}
		} header: {
			Text(L("Word lists"))
		} footer: {
			Text(status.isEmpty
				? L("Custom phrases are offered when you type their code: dz for your address, say. Import reads words exported from Haiku Pinyin or Rime, Rime's user dictionaries and custom_phrase.txt, Sogou's word lists and custom phrases, and the Mac's text replacements (the .plist you drag out of System Settings).")
				: status)
				.foregroundStyle(.secondary)
		}
	}

	private func openPhrases() {
		let path = String(cString: phrasesFile())
		NSWorkspace.shared.open(URL(fileURLWithPath: path))
	}

	private func importFile() {
		let panel = NSOpenPanel()
		panel.title = L("Import Words")
		panel.message = L("Words or custom phrases from Haiku Pinyin, Rime or Sogou, or the Mac's text replacements")
		panel.prompt = L("Import")
		panel.canChooseDirectories = false
		panel.allowsMultipleSelection = false
		NSApp.activate(ignoringOtherApps: true)
		panel.begin { response in
			guard response == .OK, let url = panel.url else { return }
			run { importSummary(url.path) }
		}
	}

	private func exportFile() {
		let panel = NSSavePanel()
		panel.title = L("Export Learned Words")
		panel.prompt = L("Export")
		panel.nameFieldStringValue = L("Haiku Pinyin words.txt")
		panel.allowedContentTypes = [.plainText]
		NSApp.activate(ignoringOtherApps: true)
		panel.begin { response in
			guard response == .OK, let url = panel.url else { return }
			run {
				let count = exportWords(url.path)
				return count < 0 ? L("Couldn't write %@.", url.lastPathComponent)
					: L("Exported %ld learned words.", Int(count))
			}
		}
	}

	// Off the main thread: an import may read the whole lexicon once.
	private func run(_ work: @escaping () -> String) {
		saveNow()
		busy = true
		DispatchQueue.global(qos: .userInitiated).async {
			let message = work()
			DispatchQueue.main.async {
				status = message
				busy = false
			}
		}
	}
}

// What an import did, in a sentence or two.
private func importSummary(_ path: String) -> String {
	var words: Int32 = 0, phrases: Int32 = 0, skipped: Int32 = 0, problem: Int32 = 0
	let format = String(cString: importWords(path, &words, &phrases, &skipped, &problem))
	// haikupinyin::ImportProblem, in order; -1: no dictionaries.
	switch problem {
	case -1: return L("Couldn't load the dictionaries.")
	case 1: return L("Couldn't read that file.")
	case 2: return L("That file isn't UTF-8 text. If it's GBK, convert it first: iconv -f GBK -t UTF-8")
	case 3: return L("That's a binary plist. Convert it first: plutil -convert xml1")
	case 4: return L("Couldn't save the learned words.")
	default: break
	}
	if format.isEmpty || (words == 0 && phrases == 0) {
		return L("Found nothing to import in that file.")
	}
	let names = [
		"Haiku Pinyin": L("Haiku Pinyin"),
		"Sogou": L("Sogou word list"),
		"Sogou custom phrases": L("Sogou custom phrases"),
		"Rime user dictionary": L("Rime user dictionary"),
		"Rime custom phrases": L("Rime custom phrases"),
		"text replacements": L("Mac text replacements"),
		"mixed": L("several formats"),
	]
	let name = names[format] ?? format
	var summary = words > 0 && phrases > 0
		? L("Imported %ld words and %ld phrases (%@).", Int(words), Int(phrases), name)
		: words > 0 ? L("Imported %ld words (%@).", Int(words), name)
		: L("Imported %ld phrases (%@).", Int(phrases), name)
	if skipped > 0 {
		summary += L(" ") + L("Skipped %ld.", Int(skipped))
	}
	return summary
}
