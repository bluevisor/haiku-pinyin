// 海酷拼音 Haiku Pinyin for macOS: the settings window's 词库 section: the
// custom phrases file, and importing and exporting learned words (Words.mm
// does the work).
import AppKit
import SwiftUI
import UniformTypeIdentifiers

// Words.mm: the phrases file (made if missing); import and export, each
// answering with a sentence to show.
@_silgen_name("HPPhrasesFile")
private func phrasesFile() -> UnsafePointer<CChar>
@_silgen_name("HPImportWords")
private func importWords(_ path: UnsafePointer<CChar>) -> UnsafePointer<CChar>
@_silgen_name("HPExportWords")
private func exportWords(_ path: UnsafePointer<CChar>) -> UnsafePointer<CChar>

struct WordsSection: View {
	@State private var status = ""
	@State private var busy = false

	var body: some View {
		Section {
			LabeledContent {
				Button("编辑…") { openPhrases() }
					.glassButton()
			} label: {
				Row(title: "自定义短语", symbol: "text.bubble.fill", color: .mint)
			}
			LabeledContent {
				HStack {
					Button("导入…") { importFile() }
						.glassButton()
					Button("导出…") { exportFile() }
						.glassButton()
				}
				.disabled(busy)
			} label: {
				Row(title: "导入 / 导出词库", symbol: "arrow.up.arrow.down.square.fill",
					color: .indigo)
			}
		} header: {
			Text("词库")
		} footer: {
			Text(status.isEmpty
				? "自定义短语：打出编码时给出的短语，如 dz 出地址。导入：海酷拼音或 Rime 导出的词、Rime 的用户词典和 custom_phrase.txt、搜狗的词库和自定义短语、Mac 的文本替换（从系统设置里拖出的 .plist）。"
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
		panel.title = "导入词库"
		panel.message = "海酷拼音、Rime、搜狗的词库或自定义短语，或 Mac 的文本替换"
		panel.prompt = "导入"
		panel.canChooseDirectories = false
		panel.allowsMultipleSelection = false
		NSApp.activate(ignoringOtherApps: true)
		panel.begin { response in
			guard response == .OK, let url = panel.url else { return }
			run { importWords(url.path) }
		}
	}

	private func exportFile() {
		let panel = NSSavePanel()
		panel.title = "导出学到的词"
		panel.prompt = "导出"
		panel.nameFieldStringValue = "海酷拼音词库.txt"
		panel.allowedContentTypes = [.plainText]
		NSApp.activate(ignoringOtherApps: true)
		panel.begin { response in
			guard response == .OK, let url = panel.url else { return }
			run { exportWords(url.path) }
		}
	}

	// Off the main thread: an import reads the whole lexicon once.
	private func run(_ work: @escaping () -> UnsafePointer<CChar>) {
		saveNow()
		busy = true
		DispatchQueue.global(qos: .userInitiated).async {
			let message = String(cString: work())
			DispatchQueue.main.async {
				status = message
				busy = false
			}
		}
	}
}
