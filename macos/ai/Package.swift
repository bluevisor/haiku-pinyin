// swift-tools-version: 6.2
// HaikuPinyinAI: the model behind 海酷拼音's AI picks and completions, a
// helper the input method starts. Build with xcodebuild (MLX's Metal shaders
// need it): macos/build.sh does.
import PackageDescription

let package = Package(
	name: "HaikuPinyinAI",
	platforms: [.macOS(.v14)],
	dependencies: [
		.package(url: "https://github.com/ml-explore/mlx-swift-lm", from: "3.32.3"),
		.package(url: "https://github.com/huggingface/swift-transformers", from: "1.4.0"),
	],
	targets: [
		.executableTarget(
			name: "HaikuPinyinAI",
			dependencies: [
				.product(name: "MLXLLM", package: "mlx-swift-lm"),
				.product(name: "MLXLMCommon", package: "mlx-swift-lm"),
				.product(name: "MLXHuggingFace", package: "mlx-swift-lm"),
				.product(name: "Tokenizers", package: "swift-transformers"),
			],
			path: "Sources",
			swiftSettings: [.swiftLanguageMode(.v5)]
		),
	]
)
