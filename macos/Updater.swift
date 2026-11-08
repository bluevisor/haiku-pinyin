// 海酷拼音 Haiku Pinyin for macOS: updating itself. Once a day (and when the
// settings window asks) it reads latest.json from the newest GitHub release
// (macos/release.sh writes it):
//   {"version": "0.1.7", "url": "…/HaikuPinyin-0.1.7-mac.zip",
//    "sha256": "…", "notes": "…", "minimumSystemVersion": "13.0"}
// A newer version is downloaded, checked against the SHA-256, unpacked and
// checked again: it must be this method, signed with the same Developer ID.
// Then it waits for the Mac to sit idle with nothing being typed, takes this
// copy's place in ~/Library/Input Methods and quits; the system starts the
// new one on the next key. Only a release copy updates itself: a build
// signed any other way (ad hoc, for development) leaves itself alone.
//   defaults write com.bluevisor.inputmethod.HaikuPinyin updateFeed <url>
// reads another latest.json (for testing).
import AppKit
import CryptoKit
import Foundation
import Security

private let defaultFeed = "https://github.com/bluevisor/haiku-pinyin/releases/latest/download/latest.json"
private let teamID = "CKGXYS3R8N"
private let checkInterval: TimeInterval = 24 * 60 * 60
private let idleBeforeInstall: TimeInterval = 120	// seconds without a key or click

// main.mm: saves the learned words; whether a composition is open.
@_silgen_name("HPSaveNow")
func saveNow()
@_silgen_name("HPComposing")
func composing() -> Bool

private struct Release: Decodable {
	let version: String
	let url: URL
	let sha256: String
	let notes: String?
	let minimumSystemVersion: String?
}

// "0.1.10" > "0.1.9".
func newer(_ a: String, than b: String) -> Bool {
	let x = a.split(separator: ".").map { Int($0) ?? 0 }
	let y = b.split(separator: ".").map { Int($0) ?? 0 }
	for i in 0 ..< max(x.count, y.count) {
		let p = i < x.count ? x[i] : 0
		let q = i < y.count ? y[i] : 0
		if p != q { return p > q }
	}
	return false
}

// This method, signed by its own Developer ID, whole.
private func signedByUs(_ bundle: URL) -> Bool {
	guard let identifier = Bundle.main.bundleIdentifier else { return false }
	var code: SecStaticCode?
	var requirement: SecRequirement?
	let text = "identifier \"\(identifier)\" and anchor apple generic"
		+ " and certificate leaf[subject.OU] = \"\(teamID)\""
	guard SecStaticCodeCreateWithPath(bundle as CFURL, [], &code) == errSecSuccess,
		SecRequirementCreateWithString(text as CFString, [], &requirement) == errSecSuccess,
		let code, let requirement else { return false }
	let flags = SecCSFlags(rawValue: kSecCSCheckAllArchitectures
		| kSecCSStrictValidate | kSecCSCheckNestedCode)
	return SecStaticCodeCheckValidityWithErrors(code, flags, requirement, nil)
		== errSecSuccess
}

final class Updater: ObservableObject {
	static let shared = Updater()

	@Published private(set) var status = ""
	@Published private(set) var busy = false
	@Published private(set) var staged: String?		// the version waiting to go in
	@Published private(set) var lastCheck: Date?

	private let defaults = UserDefaults.standard
	private var stagedApp: URL?
	private var timer: Timer?

	var version: String {
		Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString")
			as? String ?? "0"
	}

	var automatic: Bool { defaults.object(forKey: "autoUpdate") as? Bool ?? true }

	// Where updates wait, beside the models: the same disk as Input Methods,
	// so taking this copy's place is a rename.
	private var updates: URL {
		FileManager.default.urls(for: .applicationSupportDirectory,
			in: .userDomainMask)[0].appendingPathComponent("HaikuPinyin/Updates")
	}

	// A release copy, where the system runs input methods from.
	var canUpdate: Bool {
		Bundle.main.bundleURL.deletingLastPathComponent().lastPathComponent
			== "Input Methods" && signedByUs(Bundle.main.bundleURL)
	}

	init() {
		lastCheck = defaults.object(forKey: "lastUpdateCheck") as? Date
	}

	func start() {
		guard canUpdate else {
			status = "开发版，不自动更新"
			return
		}
		try? FileManager.default.removeItem(at: updates)	// half-done ones
		timer = Timer.scheduledTimer(withTimeInterval: 60, repeats: true) { [weak self] _ in
			self?.tick()
		}
	}

	private func tick() {
		if stagedApp != nil {
			installWhenIdle()
		} else if automatic && !busy
			&& -(lastCheck ?? .distantPast).timeIntervalSinceNow >= checkInterval {
			check(manual: false)
		}
	}

	func check(manual: Bool) {
		guard !busy, stagedApp == nil else { return }
		guard canUpdate else {
			status = "开发版，不自动更新"
			return
		}
		busy = true
		status = "正在检查更新…"
		let feed = URL(string: defaults.string(forKey: "updateFeed") ?? defaultFeed)!
		var request = URLRequest(url: feed, cachePolicy: .reloadIgnoringLocalCacheData,
			timeoutInterval: 30)
		request.setValue("HaikuPinyin/\(version)", forHTTPHeaderField: "User-Agent")
		URLSession.shared.dataTask(with: request) { data, response, error in
			let code = (response as? HTTPURLResponse)?.statusCode ?? 0
			let release = data.flatMap { try? JSONDecoder().decode(Release.self, from: $0) }
			DispatchQueue.main.async {
				self.lastCheck = Date()
				self.defaults.set(self.lastCheck, forKey: "lastUpdateCheck")
				guard let release, code == 200 else {
					self.finish(error != nil ? "无法连接更新服务器" : "检查更新失败（\(code)）")
					return
				}
				self.consider(release)
			}
		}.resume()
	}

	private func consider(_ release: Release) {
		guard newer(release.version, than: version) else {
			finish("已是最新版本")
			return
		}
		if let minimum = release.minimumSystemVersion {
			let parts = minimum.split(separator: ".").map { Int($0) ?? 0 }
			let needed = OperatingSystemVersion(majorVersion: parts.first ?? 0,
				minorVersion: parts.count > 1 ? parts[1] : 0, patchVersion: 0)
			if !ProcessInfo.processInfo.isOperatingSystemAtLeast(needed) {
				finish("\(release.version) 需要 macOS \(minimum)")
				return
			}
		}
		status = "正在下载 \(release.version)…"
		URLSession.shared.downloadTask(with: release.url) { file, response, error in
			let code = (response as? HTTPURLResponse)?.statusCode ?? 0
			var result: Result<URL, Error>
			if let file, code == 200 {
				result = Result { try self.unpack(file, release) }
			} else {
				result = .failure(UpdateError("下载失败（\(error?.localizedDescription ?? "\(code)")）"))
			}
			DispatchQueue.main.async {
				switch result {
				case .success(let app):
					self.stagedApp = app
					self.staged = release.version
					self.finish("\(release.version) 已下载，空闲时自动安装")
				case .failure(let error):
					self.finish(error.localizedDescription)
				}
			}
		}.resume()
	}

	// On URLSession's queue: the zip checked, unpacked and checked again.
	private func unpack(_ zip: URL, _ release: Release) throws -> URL {
		let manager = FileManager.default
		let data = try Data(contentsOf: zip, options: .mappedIfSafe)
		let digest = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
		guard digest == release.sha256.lowercased() else {
			throw UpdateError("下载的文件校验不符")
		}
		let folder = updates.appendingPathComponent(release.version)
		try? manager.removeItem(at: folder)
		try manager.createDirectory(at: folder, withIntermediateDirectories: true)
		do {
			return try check(unpacking: zip, into: folder, release)
		} catch {
			try? manager.removeItem(at: updates)
			throw error
		}
	}

	private func check(unpacking zip: URL, into folder: URL, _ release: Release) throws -> URL {
		let manager = FileManager.default
		let ditto = Process()
		ditto.executableURL = URL(fileURLWithPath: "/usr/bin/ditto")
		ditto.arguments = ["-x", "-k", zip.path, folder.path]
		try ditto.run()
		ditto.waitUntilExit()
		let app = folder.appendingPathComponent(Bundle.main.bundleURL.lastPathComponent)
		guard ditto.terminationStatus == 0, manager.fileExists(atPath: app.path) else {
			throw UpdateError("无法解压更新")
		}
		let info = Bundle(url: app)?.object(forInfoDictionaryKey:
			"CFBundleShortVersionString") as? String
		guard info == release.version else {
			throw UpdateError("更新的版本号不符")
		}
		guard signedByUs(app) else {
			throw UpdateError("更新的签名不对，已丢弃")
		}
		return app
	}

	private func finish(_ message: String) {
		busy = false
		status = message
	}

	private var idle: TimeInterval {
		CGEventSource.secondsSinceLastEventType(.combinedSessionState,
			eventType: CGEventType(rawValue: ~0)!)
	}

	private func installWhenIdle() {
		guard idle >= idleBeforeInstall, !composing(),
			NSApp.keyWindow == nil else { return }
		installNow()
	}

	// Takes this copy's place and quits; the system starts the new one.
	func installNow() {
		guard let app = stagedApp, !composing() else { return }
		do {
			saveNow()
			_ = try FileManager.default.replaceItemAt(Bundle.main.bundleURL,
				withItemAt: app)
		} catch {
			stagedApp = nil
			staged = nil
			status = "安装更新失败：\(error.localizedDescription)"
			return
		}
		try? FileManager.default.removeItem(at: updates)
		NSLog("Haiku Pinyin: updated to %@; quitting for the new copy", staged ?? "?")
		exit(0)
	}
}

private struct UpdateError: LocalizedError {
	let errorDescription: String?
	init(_ message: String) { errorDescription = message }
}

@_cdecl("HPUpdaterStart")
public func updaterStart() {
	Updater.shared.start()
}

// HaikuPinyin --update: checks now and, with a newer release, takes this
// copy's place at once (and exits 0). Otherwise says why not; 1 on failure.
@_cdecl("HPUpdaterRunOnce")
public func updaterRunOnce() -> Int32 {
	let updater = Updater.shared
	updater.check(manual: true)
	let limit = Date().addingTimeInterval(600)
	while updater.busy && Date() < limit {
		RunLoop.main.run(until: Date().addingTimeInterval(0.2))
	}
	print(updater.status)
	if updater.staged != nil {
		updater.installNow()	// exits when it's done
		print(updater.status)
		return 1
	}
	return updater.status == "已是最新版本" ? 0 : 1
}
