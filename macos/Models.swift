// 海酷拼音 Haiku Pinyin for macOS: downloading the AI's models from the
// settings window. Qwen's base models, converted to 4-bit MLX (macos/ai/
// fetch-models.sh, uploaded by macos/ai/upload-models.sh), come from Hugging
// Face, or its mirror when that can't be reached, a file at a time into a
// hidden folder beside where they go. A file already there whole is kept, a
// part of one is carried on from where it stopped, and each is checked
// against its SHA-256 before the folder takes the model's name.
//   defaults write com.bluevisor.inputmethod.HaikuPinyin modelBaseURL <url>
// fetches <url>/<model>/<file> instead (for testing).
import CryptoKit
import Foundation

struct ModelFile {
	let name: String
	let size: Int64
	let sha256: String
}

struct ModelSpec {
	let name: String		// the folder in the models directory, as AI.mm has it
	let repo: String		// on Hugging Face
	let files: [ModelFile]

	var size: Int64 { files.reduce(0) { $0 + $1.size } }
}

// The default model first, then its backup (AI.mm's kModel and kBackup).
let modelSpecs = [
	ModelSpec(name: "Qwen3-1.7B-Base-4bit", repo: "bluevisor/Qwen3-1.7B-Base-4bit-mlx",
		files: [
			ModelFile(name: "config.json", size: 989,
				sha256: "442f6bc940995fd67c970b4190030c4699afbca9a40905b2a32f1b1d5532e93a"),
			ModelFile(name: "generation_config.json", size: 138,
				sha256: "8c970692323e3ea0e9b8b0a4dca79388d31226e41f83c9fd6014804280ebf6e8"),
			ModelFile(name: "chat_template.jinja", size: 4116,
				sha256: "87a2728cb8dc9fe424d624542f6060ec05a1d285ebbec578bb078900e33396b5"),
			ModelFile(name: "tokenizer_config.json", size: 732,
				sha256: "0ef40ad4946b9af6b4faec11f4551a32c6595fbee6ee67788e3e79ede3838a04"),
			ModelFile(name: "tokenizer.json", size: 11_422_650,
				sha256: "be75606093db2094d7cd20f3c2f385c212750648bd6ea4fb2bf507a6a4c55506"),
			ModelFile(name: "model.safetensors.index.json", size: 49771,
				sha256: "b2e5b0437ba3dd97c262018713af2fea5c010a8a95d6e24ee6dd045f4cac8ce0"),
			ModelFile(name: "model.safetensors", size: 968_080_210,
				sha256: "3b29904336e6b2fdfb80af60541e23a14b3e729de80df0ca178f661a9d0f448d"),
		]),
	ModelSpec(name: "Qwen3.5-0.8B-Base-4bit", repo: "bluevisor/Qwen3.5-0.8B-Base-4bit-mlx",
		files: [
			ModelFile(name: "config.json", size: 2625,
				sha256: "919d818806acc6093475afcccc40b4c27deb509ef1508ba940c71c9b3ce83ab5"),
			ModelFile(name: "chat_template.jinja", size: 7755,
				sha256: "273d8e0e683b885071fb17e08d71e5f2a5ddfb5309756181681de4f5a1822d80"),
			ModelFile(name: "tokenizer_config.json", size: 1505,
				sha256: "ce915a484ffe99d34d38a925d07e291d43846c241431304512c7fadb9287d168"),
			ModelFile(name: "tokenizer.json", size: 19_989_339,
				sha256: "a5cd9732badce41de57e6efce8302930ded1c1188c5f81feb2bd6c24c4a1941f"),
			ModelFile(name: "model.safetensors.index.json", size: 60784,
				sha256: "d95a2e17af08ab870498266088eccee4d0a012338c604465ae7df24b976c488e"),
			ModelFile(name: "model.safetensors", size: 424_026_782,
				sha256: "8d7883bce060432705aa0e48e8743babd47d7c3d6ca89e8d63574549b52ea3ad"),
		]),
]

// AI.mm: restarts the helper with what's in the models directory now.
@_silgen_name("HPAIModelsChanged")
func aiModelsChanged()

// AI.mm: the helper and the chosen model are there, and it didn't fail to
// load.
@_silgen_name("HPAIUsable")
func aiUsableNow() -> Bool

@_silgen_name("HPAIFailed")
func aiFailedNow() -> Bool

// ~/Library/Application Support/HaikuPinyin/models, or $HAIKU_PINYIN_MODELS,
// as AI.mm has it.
func modelsFolder() -> URL {
	if let path = ProcessInfo.processInfo.environment["HAIKU_PINYIN_MODELS"] {
		return URL(fileURLWithPath: path)
	}
	return FileManager.default.urls(for: .applicationSupportDirectory,
		in: .userDomainMask)[0].appendingPathComponent("HaikuPinyin/models")
}

// The AI runs on Apple silicon only (MLX), with the helper the app was
// built with.
var aiSupported: Bool {
	var arm = 0
	var size = MemoryLayout<Int>.size
	sysctlbyname("hw.optional.arm64", &arm, &size, nil, 0)
	return arm == 1 && Bundle.main.path(forAuxiliaryExecutable: "HaikuPinyinAI") != nil
}

func modelInstalled(_ spec: ModelSpec) -> Bool {
	FileManager.default.fileExists(atPath: modelsFolder()
		.appendingPathComponent(spec.name).appendingPathComponent("config.json").path)
}

private enum DownloadError: LocalizedError {
	case status(Int)
	case mismatch(String)
	case cancelled

	var errorDescription: String? {
		switch self {
		case .status(let code): return "服务器返回 \(code)"
		case .mismatch(let file): return "\(file) 校验不符"
		case .cancelled: return "已取消"
		}
	}
}

final class ModelDownloader: NSObject, ObservableObject, URLSessionDataDelegate {
	static let shared = ModelDownloader()

	@Published private(set) var running = false
	@Published private(set) var done: Int64 = 0		// bytes, of all the models missing
	@Published private(set) var total: Int64 = 0
	@Published private(set) var message = ""		// what it's doing, or what went wrong

	var missing: [ModelSpec] { modelSpecs.filter { !modelInstalled($0) } }

	private lazy var session = URLSession(configuration: .default, delegate: self,
		delegateQueue: nil)
	private var task: URLSessionDataTask?
	private var cancelled = false
	// The file being written, its bytes so far, and how the task ends.
	private var handle: FileHandle?
	private var written: Int64 = 0
	private var base: Int64 = 0			// done before this file
	private var finished: ((Error?) -> Void)?

	func start() {
		guard !running else { return }
		let specs = missing
		guard !specs.isEmpty else { return }
		running = true
		cancelled = false
		total = specs.reduce(0) { $0 + $1.size }
		done = 0
		message = "正在下载…"
		Thread.detachNewThread { self.run(specs) }
	}

	func cancel() {
		cancelled = true
		task?.cancel()
	}

	// On its own thread: one file at a time.
	private func run(_ specs: [ModelSpec]) {
		var failure: Error?
		var count: Int64 = 0
		for spec in specs {
			do {
				try fetch(spec, &count)
			} catch {
				failure = error
				break
			}
		}
		DispatchQueue.main.async {
			self.running = false
			self.task = nil
			if let failure {
				self.message = "下载失败：\(failure.localizedDescription)"
			} else {
				self.message = "已下载"
				aiModelsChanged()
			}
		}
	}

	private func fetch(_ spec: ModelSpec, _ count: inout Int64) throws {
		let manager = FileManager.default
		let folder = modelsFolder()
		let partial = folder.appendingPathComponent(".\(spec.name).partial")
		try manager.createDirectory(at: partial, withIntermediateDirectories: true)
		for file in spec.files {
			let destination = partial.appendingPathComponent(file.name)
			let size = (try? manager.attributesOfItem(atPath: destination.path)[.size])
				as? Int64 ?? 0
			if size == file.size, (try? sha256Hex(destination)) == file.sha256 {
				count += file.size
				publish(count)
				continue
			}
			if size >= file.size {
				try? manager.removeItem(at: destination)
			}
			try download(spec, file, to: destination, base: count)
			if try sha256Hex(destination) != file.sha256 {
				try? manager.removeItem(at: destination)
				throw DownloadError.mismatch(file.name)
			}
			count += file.size
			publish(count)
		}
		let final = folder.appendingPathComponent(spec.name)
		// A link or a folder of that name left from before goes.
		if (try? manager.destinationOfSymbolicLink(atPath: final.path)) != nil
			|| manager.fileExists(atPath: final.path) {
			try manager.removeItem(at: final)
		}
		try manager.moveItem(at: partial, to: final)
	}

	private func publish(_ count: Int64) {
		DispatchQueue.main.async { self.done = count }
	}

	// Hugging Face first, then the mirror; a test URL alone.
	private func sources(_ spec: ModelSpec, _ file: ModelFile) -> [URL] {
		if let base = UserDefaults.standard.string(forKey: "modelBaseURL"),
			let url = URL(string: base) {
			return [url.appendingPathComponent(spec.name).appendingPathComponent(file.name)]
		}
		return ["https://huggingface.co", "https://hf-mirror.com"].compactMap {
			URL(string: "\($0)/\(spec.repo)/resolve/main/\(file.name)")
		}
	}

	private func download(_ spec: ModelSpec, _ file: ModelFile, to destination: URL,
		base count: Int64) throws {
		var last: Error = DownloadError.status(0)
		for url in sources(spec, file) {
			if cancelled { throw DownloadError.cancelled }
			do {
				try download(url, to: destination, base: count)
				return
			} catch {
				if cancelled { throw DownloadError.cancelled }
				last = error
			}
		}
		throw last
	}

	// One URL into destination, carrying on after what's there.
	private func download(_ url: URL, to destination: URL, base count: Int64) throws {
		let manager = FileManager.default
		if !manager.fileExists(atPath: destination.path) {
			manager.createFile(atPath: destination.path, contents: nil)
		}
		let handle = try FileHandle(forWritingTo: destination)
		defer { try? handle.close() }
		let have = try handle.seekToEnd()
		var request = URLRequest(url: url, timeoutInterval: 60)
		if have > 0 {
			request.setValue("bytes=\(have)-", forHTTPHeaderField: "Range")
		}
		let ended = DispatchSemaphore(value: 0)
		var result: Error?
		self.handle = handle
		written = Int64(have)
		base = count
		finished = { error in
			result = error
			ended.signal()
		}
		let task = session.dataTask(with: request)
		self.task = task
		task.resume()
		ended.wait()
		self.handle = nil
		if let result { throw result }
	}

	func urlSession(_ session: URLSession, dataTask: URLSessionDataTask,
		didReceive response: URLResponse,
		completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
		let status = (response as? HTTPURLResponse)?.statusCode ?? 0
		switch status {
		case 206:
			break
		case 200:
			// The whole file again (the server ignored the range).
			try? handle?.truncate(atOffset: 0)
			written = 0
		default:
			finished?(DownloadError.status(status))
			finished = nil
			completionHandler(.cancel)
			return
		}
		completionHandler(.allow)
	}

	func urlSession(_ session: URLSession, dataTask: URLSessionDataTask,
		didReceive data: Data) {
		guard let handle else { return }
		do {
			try handle.write(contentsOf: data)
		} catch {
			finished?(error)
			finished = nil
			dataTask.cancel()
			return
		}
		written += Int64(data.count)
		publish(base + written)
	}

	func urlSession(_ session: URLSession, task: URLSessionTask,
		didCompleteWithError error: Error?) {
		finished?(cancelled ? DownloadError.cancelled : error)
		finished = nil
	}
}

private func sha256Hex(_ url: URL) throws -> String {
	let handle = try FileHandle(forReadingFrom: url)
	defer { try? handle.close() }
	var hasher = SHA256()
	while let chunk = try handle.read(upToCount: 4 << 20), !chunk.isEmpty {
		hasher.update(data: chunk)
	}
	return hasher.finalize().map { String(format: "%02x", $0) }.joined()
}
