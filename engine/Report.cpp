// Haiku Pinyin: the link to a prefilled bug report.

#include "Report.h"

namespace haikupinyin {

static const char* const kIssueUrl
	= "https://github.com/bluevisor/haiku-pinyin/issues/new";


static std::string
Escape(const std::string& text)
{
	static const char* const kHex = "0123456789ABCDEF";
	std::string escaped;
	for (unsigned char c : text) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
			|| (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'
			|| c == '~') {
			escaped += char(c);
		} else {
			escaped += '%';
			escaped += kHex[c >> 4];
			escaped += kHex[c & 15];
		}
	}
	return escaped;
}


std::string
ReportLayout(const std::string& layout)
{
	static const struct {
		const char* layout;
		const char* option;
	} kOptions[] = {
		{"full", "全拼 Full pinyin"},
		{"pyjj", "双拼 · 拼音加加"},
		{"xiaohe", "双拼 · 小鹤"},
		{"ziranma", "双拼 · 自然码"},
		{"mspy", "双拼 · 微软"},
		{"abc", "双拼 · 智能ABC"},
	};
	for (const auto& option : kOptions) {
		if (layout == option.layout)
			return option.option;
	}
	return kOptions[0].option;
}


std::string
ReportSettings(const Options& options)
{
	auto on = [](bool value) { return value ? "on" : "off"; };
	std::string text = std::string("简/繁: ")
		+ (options.script == Script::kSimplified ? "简体" : "繁體") + "\n"
		+ "page size: " + std::to_string(options.pageSize) + "\n"
		+ "全角标点: " + on(options.fullwidthPunctuation) + "\n"
		+ "emoji: " + on(options.emoji) + "\n"
		+ "中英混输: " + on(options.english) + "\n"
		+ "云候选: " + on(options.cloud) + "\n";
	std::string fuzzy;
	static const struct {
		unsigned mask;
		const char* label;
	} kFuzzy[] = {
		{kFuzzyRetroflex, "z=zh c=ch s=sh"},
		{kFuzzyNasal, "an=ang en=eng in=ing"},
		{kFuzzyNL, "n=l"},
		{kFuzzyFH, "f=h"},
		{kFuzzyRL, "r=l"},
	};
	for (const auto& group : kFuzzy) {
		if ((options.fuzzy & group.mask) == group.mask)
			fuzzy += (fuzzy.empty() ? "" : ", ") + std::string(group.label);
	}
	text += "模糊音: " + (fuzzy.empty() ? std::string("off") : fuzzy);
	if (options.layout != "full") {
		static const char* const kVowels[] = {"aa oo ee", "oa oo oe", "a o e"};
		text += std::string("\n双拼 a o e: ") + kVowels[int(options.vowels)];
	}
	return text;
}


std::string
ReportUrl(const std::string& platform, const std::string& version,
	const std::string& system, const Options& options)
{
	return std::string(kIssueUrl) + "?template=bug_report.yml"
		+ "&platform=" + Escape(platform)
		+ "&version=" + Escape(version)
		+ "&system=" + Escape(system)
		+ "&layout=" + Escape(ReportLayout(options.layout))
		+ "&settings=" + Escape(ReportSettings(options));
}

}	// namespace haikupinyin
