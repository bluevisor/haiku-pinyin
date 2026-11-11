// 海酷拼音 Haiku Pinyin for macOS: the words of the windows, in the system's
// language as the input menu has it (Menu in InputController.mm): 简体 or
// 繁體 Chinese, else English. The tables are Resources/zh-Hans.lproj and
// zh-Hant.lproj/Localizable.strings, keyed by the English; check-strings.py
// checks them against the code.
import Foundation

// "en", "zh-Hans" or "zh-Hant".
let uiLanguage = Bundle.preferredLocalizations(from: ["en", "zh-Hans", "zh-Hant"],
	forPreferences: Locale.preferredLanguages).first ?? "en"

private let table = Bundle.main.path(forResource: uiLanguage, ofType: "lproj")
	.flatMap { Bundle(path: $0) }

// key in that language, or the key itself when the table hasn't got it. With
// arguments, it's a format. Text(String) and Button(String) show it as it
// is; only a literal is looked up, and in another table.
func L(_ key: String, _ arguments: CVarArg...) -> String {
	let text = table?.localizedString(forKey: key, value: key, table: nil) ?? key
	return arguments.isEmpty ? text : String(format: text, arguments: arguments)
}
