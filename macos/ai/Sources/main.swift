// HaikuPinyinAI: the model behind 海酷拼音's AI picks and completions.
// The input method starts it and talks to it in JSON lines:
//   {"id": 1, "op": "rank", "prompt": "我妹妹说", "options": ["他", "她"]}
//     -> {"id": 1, "scores": [-6.1, -2.3], "ms": 9, "model": "..."}
//       ln P(option | prompt) for each option
//   {"id": 2, "op": "complete", "prompt": "祝你生日", "threshold": 0.5}
//     -> {"id": 2, "text": "快乐", "ms": 14, "model": "..."}
//       the likeliest continuation, a character at a time while the chance
//       it is all right stays above threshold, never past the sentence
// Only the newest request of each kind waiting is answered: typing moves on.
//
//   HaikuPinyinAI <model directory> [<backup model directory>]
// The backup (a smaller model) takes over when the first won't load, or is
// slower than the budget on this Mac.
import Foundation
import MLX
import MLXHuggingFace
import MLXLLM
import MLXLMCommon
import Tokenizers

setvbuf(stdout, nil, _IOLBF, 0)

func log(_ message: String) {
	FileHandle.standardError.write(("HaikuPinyinAI: " + message + "\n").data(using: .utf8)!)
}

struct Request: Decodable {
	let id: Int
	let op: String
	let prompt: String?
	let options: [String]?
	let threshold: Double?
	let maxCharacters: Int?
	let trace: Bool?
}

// The end of a suggestion.
let stops = Set("\n。！？!?")

final class Model {
	let name: String
	private let context: ModelContext
	private var cache: [KVCache]			// exactly the tokens in cached
	private var cached: [Int] = []
	private var lastLogits: MLXArray?		// after the last of them

	init(directory: URL) async throws {
		context = try await LLMModelFactory.shared.load(from: directory,
			using: #huggingFaceTokenizerLoader())
		cache = try context.model.newCache(parameters: nil)
		name = directory.lastPathComponent
		// The first run compiles the GPU kernels: do it now, not on a key.
		_ = rank(prompt: "你好", options: ["世界", "中国"])
		if let logits = lastLogits {
			indexTokens(logits.dim(-1))
		}
		_ = complete(prompt: "你好", threshold: 0.5, maxCharacters: 2)
	}

	private func encode(_ text: String) -> [Int] {
		context.tokenizer.encode(text: text, addSpecialTokens: false)
	}

	private var trimmable: Bool { cache.allSatisfy { $0.isTrimmable } }

	// Logits, in full precision: the scores are compared closely.
	private func forward(_ tokens: [Int], _ cache: [KVCache]) -> MLXArray {
		let input = MLXArray(tokens.map { Int32($0) }).reshaped(1, tokens.count)
		return context.model(input, cache: cache)[0].asType(.float32)
	}

	// A cache to run past the prompt with, the prompt's left as it was after:
	// rewound where the cache can be (attention), else a copy (Qwen3.5's
	// linear layers keep a state that can't be).
	private func scratch<T>(_ body: ([KVCache]) -> (T, added: Int)) -> T {
		if trimmable {
			let (result, added) = body(cache)
			for layer in cache where added > 0 {
				_ = layer.trim(added)
			}
			return result
		}
		return body(cache.map { $0.copy() }).0
	}

	// The cache holds the prompt and lastLogits what comes after it. Only what
	// differs from the last prompt is run: as the text grows, little is.
	// It starts on a new line: a base model given a first token with nothing
	// before it is lost (中华人民 alone goes on as "icon").
	private func prefill(_ text: String) {
		let tokens = encode("\n" + text)
		var same = 0
		while same < tokens.count && same < cached.count && tokens[same] == cached[same] {
			same += 1
		}
		if same == tokens.count && same == cached.count && lastLogits != nil {
			return
		}
		// At least the last token runs, for the logits after it.
		same = min(same, tokens.count - 1)
		if same < cached.count {
			if trimmable {
				for layer in cache {
					_ = layer.trim(cached.count - same)
				}
			} else {
				cache = (try? context.model.newCache(parameters: nil)) ?? cache
				same = 0
			}
		}
		let logits = forward(Array(tokens[same...]), cache)
		lastLogits = logits[logits.dim(0) - 1]
		cached = tokens
	}

	private static func logSoftmax(_ logits: MLXArray) -> MLXArray {
		logits - logSumExp(logits, axis: -1, keepDims: true)
	}

	func rank(prompt: String, options: [String]) -> [Double] {
		prefill(prompt)
		guard let after = lastLogits else { return options.map { _ in -.infinity } }
		let first = Model.logSoftmax(after)
		return options.map { option in
			let ids = encode(option)
			guard !ids.isEmpty else { return -.infinity }
			var total = Double(first[ids[0]].item(Float.self))
			if ids.count > 1 {
				total += scratch { cache in
					let logprobs = Model.logSoftmax(forward(Array(ids.dropLast()), cache))
					var sum = 0.0
					for j in 0 ..< ids.count - 1 {
						sum += Double(logprobs[j, ids[j + 1]].item(Float.self))
					}
					return (sum, ids.count - 1)
				}
			}
			return total
		}
	}

	// Each token's text, nil for pieces of a character (byte tokens).
	private var texts: [Int: String?] = [:]

	private func text(_ token: Int) -> String? {
		if let known = texts[token] {
			return known
		}
		let decoded = context.tokenizer.decode(tokenIds: [token], skipSpecialTokens: true)
		let text = decoded.isEmpty || decoded.contains("\u{FFFD}") ? nil : decoded
		texts[token] = text
		return text
	}

	// Every token with text, by its first character: what a token the prompt
	// ends inside of could be.
	private var byFirst: [Character: [Int32]] = [:]

	private func indexTokens(_ count: Int) {
		for id in 0 ..< count {
			if let text = text(id), let first = text.first {
				byFirst[first, default: []].append(Int32(id))
			}
		}
	}

	// The tokens after before whose text starts with part, all of them.
	private func healingTokens(startingWith part: String, after before: String) -> [(String, Double)] {
		guard let first = part.first, let ids = byFirst[first] else { return [] }
		let matching = ids.filter { text(Int($0))!.hasPrefix(part) }
		guard !matching.isEmpty else { return [] }
		prefill(before)
		guard let after = lastLogits else { return [] }
		let values = softmax(after, axis: -1)[MLXArray(matching)].asArray(Float.self)
		return zip(matching, values).map { (text(Int($0))!, Double($1)) }
	}

	// The likeliest next tokens after the prompt, as text.
	private func nextTokens(_ prompt: String) -> [(String, Double)] {
		prefill(prompt)
		guard let after = lastLogits else { return [] }
		let probabilities = softmax(after, axis: -1)
		let top = argPartition(-probabilities, kth: 255)[..<256]
		let values = probabilities[top].asArray(Float.self)
		return zip(top.asArray(Int32.self), values).compactMap { id, value in
			text(Int(id)).map { ($0, Double(value)) }
		}
	}

	// How likely each character is to come after part, the start of the next
	// token: the tokens that go on past it (共和 -> 国民, 国内, 国家… all say
	// 国), and, when tokens end right at it, what follows those.
	private func characters(after part: String, of tokens: [(String, Double)],
		mass: Double, boundary: String) -> (inside: [Character: Double],
			ending: Double, next: [(String, Double)]) {
		var inside: [Character: Double] = [:]
		var ending = 0.0
		for (text, probability) in tokens where text.hasPrefix(part) {
			if text == part {
				ending += probability
			} else {
				inside[text.dropFirst(part.count).first!, default: 0] += probability
			}
		}
		let next = !part.isEmpty && ending > 0.02 * mass ? nextTokens(boundary + part) : []
		return (inside, ending, next)
	}

	// Each step's likeliest characters, for tuning: [[character, probability]].
	var steps: [[[Any]]] = []

	// The likeliest continuation, a character at a time while the chance it's
	// all right stays above threshold; never past the end of the sentence.
	// Characters are read inside multi-character tokens: after 祝你生日 the
	// model says 快乐 as one token, and given a lone 快 it is lost.
	func complete(prompt: String, threshold: Double, maxCharacters: Int,
		trace: Bool = false) -> String {
		steps = []
		var boundary = prompt		// where the next token starts
		var part = ""				// that token so far
		var tokens: [(String, Double)] = []
		var mass = 1.0				// of the tokens that start with part
		// The prompt may end inside a token: the model writes 中华人民共和国,
		// 北京大学 and 一带一路 as one token each, so after 中华人民 or 北京大
		// it is lost. Back up over the last tokens (the most that some token
		// goes on past) and follow the tokens that start with them.
		let ids = encode(prompt)
		for count in stride(from: min(3, ids.count), through: 1, by: -1) {
			let tail = context.tokenizer.decode(tokenIds: Array(ids.suffix(count)),
				skipSpecialTokens: true)
			guard !tail.isEmpty, tail.count <= 4, prompt.hasSuffix(tail) else { continue }
			let before = String(prompt.dropLast(tail.count))
			let healed = healingTokens(startingWith: tail, after: before)
			guard healed.contains(where: { $0.0.count > tail.count }) else { continue }
			boundary = before
			part = tail
			tokens = healed
			mass = healed.reduce(0) { $0 + $1.1 }
			break
		}
		if part.isEmpty || mass <= 0 {
			boundary = prompt
			part = ""
			tokens = nextTokens(boundary)
			mass = 1
		}
		var chance = 1.0
		while (boundary.count - prompt.count) + part.count < maxCharacters {
			let (inside, ending, next) = characters(after: part, of: tokens,
				mass: mass, boundary: boundary)
			var after: [Character: Double] = [:]	// a new token's first one
			var nextMass: [Character: Double] = [:]
			for (text, probability) in next {
				after[text.first!, default: 0] += probability
			}
			nextMass = after
			var all = inside.mapValues { $0 / mass }
			for (character, probability) in after {
				all[character, default: 0] += ending * probability / mass
			}
			let ranked = all.sorted { $0.value > $1.value }
			if trace {
				steps.append(ranked.prefix(5).map { [String($0.key), $0.value] })
			}
			guard let (character, probability) = ranked.first.map({ ($0.key, $0.value) }),
				!stops.contains(character), !character.isWhitespace,
				!character.isPunctuation, chance * probability >= threshold else {
				break
			}
			chance *= probability
			// Follow the likelier way of spelling it.
			if inside[character, default: 0] >= ending * after[character, default: 0] {
				part.append(character)
				mass = inside[character, default: 0]
			} else {
				boundary += part
				part = String(character)
				tokens = next
				mass = nextMass[character, default: 0]
			}
		}
		return String((boundary + part).dropFirst(prompt.count))
	}
}

// #pragma mark - requests

final class Pending {
	private let lock = NSCondition()
	private var waiting: [String: Request] = [:]
	private var closed = false

	func put(_ request: Request) {
		lock.lock()
		waiting[request.op] = request		// the newest of its kind
		lock.signal()
		lock.unlock()
	}

	func close() {
		lock.lock()
		closed = true
		lock.signal()
		lock.unlock()
	}

	// Picks first, they're what's on screen; nil once stdin has closed.
	func take() -> Request? {
		lock.lock()
		defer { lock.unlock() }
		while waiting.isEmpty && !closed {
			lock.wait()
		}
		for op in ["rank", "complete"] {
			if let request = waiting.removeValue(forKey: op) {
				return request
			}
		}
		return waiting.isEmpty ? nil : waiting.removeValue(forKey: waiting.keys.first!)
	}
}

func reply(_ object: [String: Any]) {
	guard let data = try? JSONSerialization.data(withJSONObject: object),
		let line = String(data: data, encoding: .utf8) else { return }
	print(line)
}

func load(_ path: String) -> Model? {
	let done = DispatchSemaphore(value: 0)
	var model: Model?
	Task {
		do {
			model = try await Model(directory: URL(fileURLWithPath: path))
		} catch {
			log("can't load \(path): \(error)")
		}
		done.signal()
	}
	done.wait()
	return model
}

let arguments = CommandLine.arguments
guard arguments.count > 1 else {
	log("usage: HaikuPinyinAI <model directory> [<backup model directory>]")
	exit(2)
}
// Inside the app, MLX's GPU kernels are in its Resources.
if let executable = Bundle.main.executableURL?.resolvingSymlinksInPath() {
	let kernels = executable.deletingLastPathComponent().deletingLastPathComponent()
		.appendingPathComponent("Resources/mlx.metallib")
	if FileManager.default.fileExists(atPath: kernels.path) {
		MLX.GPU.metallib = kernels
	}
}
// The models only need a little memory beyond their weights.
MLX.GPU.set(cacheLimit: 64 * 1024 * 1024)

var model = load(arguments[1])
var backup = arguments.count > 2 ? arguments[2] : nil
if model == nil, let path = backup {
	model = load(path)
	backup = nil
}
guard var model else {
	reply(["event": "failed"])
	exit(1)
}
log("loaded \(model.name)")
reply(["event": "ready", "model": model.name])

let pending = Pending()
Thread.detachNewThread {
	let decoder = JSONDecoder()
	while let line = readLine() {
		if let data = line.data(using: .utf8),
			let request = try? decoder.decode(Request.self, from: data) {
			pending.put(request)
		}
	}
	pending.close()
}

// Too slow on this Mac for typing: the backup takes over.
let budget = 0.12		// seconds, a request's median
var recent: [Double] = []

while let request = pending.take() {
	let start = Date()
	var answer: [String: Any] = ["id": request.id]
	switch request.op {
	case "rank":
		answer["scores"] = model.rank(prompt: request.prompt ?? "",
			options: request.options ?? [])
	case "complete":
		answer["text"] = model.complete(prompt: request.prompt ?? "",
			threshold: request.threshold ?? 0.5, maxCharacters: request.maxCharacters ?? 8,
			trace: request.trace ?? false)
		if request.trace ?? false {
			answer["steps"] = model.steps
		}
	default:
		answer["error"] = "unknown op"
	}
	let elapsed = Date().timeIntervalSince(start)
	answer["ms"] = Int(elapsed * 1000)
	answer["model"] = model.name
	reply(answer)

	recent.append(elapsed)
	if recent.count > 20 {
		recent.removeFirst()
	}
	if recent.count == 20, let path = backup,
		recent.sorted()[10] > budget, let smaller = load(path) {
		log("\(model.name) is slow here (\(Int(recent.sorted()[10] * 1000)) ms): \(smaller.name) takes over")
		model = smaller
		backup = nil
		recent.removeAll()
		reply(["event": "ready", "model": model.name])
	}
}
