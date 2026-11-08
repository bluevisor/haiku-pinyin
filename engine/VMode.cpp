#include "VMode.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace haikupinyin {

static const char* const kLower[] = {"零", "一", "二", "三", "四", "五", "六",
	"七", "八", "九"};
static const char* const kUpper[] = {"零", "壹", "贰", "叁", "肆", "伍", "陆",
	"柒", "捌", "玖"};
// Digit by digit, as years are read: 二〇二六.
static const char* const kDigits[] = {"〇", "一", "二", "三", "四", "五", "六",
	"七", "八", "九"};
static const char* const kWeekdays[] = {"日", "一", "二", "三", "四", "五",
	"六"};


// The symbols, a code each. Spaces between them.
static const struct {
	const char* code;
	const char* name;
	const char* symbols;
} kSymbols[] = {
	{"bd", "标点", "， 。 、 ； ： ？ ！ … — · ～ “” ‘’ 「」 『』 《》 〈〉 【】 〔〕 （）"},
	{"kh", "括号", "（） ［］ ｛｝ 「」 『』 《》 〈〉 【】 〔〕 〖〗 ⟨⟩"},
	{"jt", "箭头", "→ ← ↑ ↓ ↔ ↕ ↖ ↗ ↘ ↙ ⇒ ⇐ ⇑ ⇓ ⇔ ➔ ➜ ↩ ↪ ⟶ ⟵"},
	{"sx", "数学", "± × ÷ ≈ ≠ ≤ ≥ ∞ √ ∑ ∏ ∫ ∂ ∈ ∉ ⊂ ⊃ ∩ ∪ ∠ ⊥ ∥ ∵ ∴ ° ‰ π ∀ ∃ ∅ ≡"},
	{"dw", "单位", "℃ ℉ ° ㎎ ㎏ ㎜ ㎝ ㎞ ㎡ ㎥ ㏄ ㎖ ㎗ ㎘ ㎐ ㎑ ㎒ Ω μ ‰ ㏒ ㏑"},
	{"hb", "货币", "¥ ￥ $ € £ ¢ ₩ ₽ ₹ ฿ ₿ ₫ ₺"},
	{"xl", "希腊", "α β γ δ ε ζ η θ ι κ λ μ ν ξ ο π ρ σ τ υ φ χ ψ ω Α Β Γ Δ Ε Ζ "
		"Η Θ Ι Κ Λ Μ Ν Ξ Ο Π Ρ Σ Τ Υ Φ Χ Ψ Ω"},
	{"xh", "序号", "① ② ③ ④ ⑤ ⑥ ⑦ ⑧ ⑨ ⑩ ⑴ ⑵ ⑶ ⑷ ⑸ ⑹ ⑺ ⑻ ⑼ ⑽ ⒈ ⒉ ⒊ ⒋ ⒌ ⒍ ⒎ ⒏ ⒐ ⒑ "
		"㈠ ㈡ ㈢ ㈣ ㈤ ㈥ ㈦ ㈧ ㈨ ㈩ Ⅰ Ⅱ Ⅲ Ⅳ Ⅴ Ⅵ Ⅶ Ⅷ Ⅸ Ⅹ ⅰ ⅱ ⅲ ⅳ ⅴ ⅵ ⅶ ⅷ ⅸ ⅹ"},
	{"ts", "特殊", "★ ☆ ● ○ ◎ ◆ ◇ ■ □ ▲ △ ▼ ▽ ※ § № ♀ ♂ ♠ ♥ ♣ ♦ ♪ ♫ ✓ ✔ ✗ ✘ ☑ ☐ "
		"☀ ☁ ☂ ☎ ✉ ♨ ☯"},
	{"py", "拼音", "ā á ǎ à ō ó ǒ ò ē é ě è ī í ǐ ì ū ú ǔ ù ǖ ǘ ǚ ǜ ü ê"},
	{"zy", "注音", "ㄅ ㄆ ㄇ ㄈ ㄉ ㄊ ㄋ ㄌ ㄍ ㄎ ㄏ ㄐ ㄑ ㄒ ㄓ ㄔ ㄕ ㄖ ㄗ ㄘ ㄙ ㄧ ㄨ ㄩ ㄚ ㄛ "
		"ㄜ ㄝ ㄞ ㄟ ㄠ ㄡ ㄢ ㄣ ㄤ ㄥ ㄦ ˉ ˊ ˇ ˋ ˙"},
};


std::vector<VCode>
VCodes()
{
	std::vector<VCode> codes = {{"rq", "日期"}, {"sj", "时间"},
		{"xq", "星期"}};
	for (const auto& group : kSymbols)
		codes.push_back({group.code, group.name});
	return codes;
}


static bool
IsDigit(char c)
{
	return c >= '0' && c <= '9';
}


bool
VNumeric(const std::string& body)
{
	return !body.empty() && (IsDigit(body[0]) || body[0] == '(');
}


static std::string
TwoDigits(int value)
{
	char text[16];
	snprintf(text, sizeof(text), "%02d", value);
	return text;
}


static std::string
DigitByDigit(const std::string& digits)
{
	std::string text;
	for (char c : digits)
		text += kDigits[c - '0'];
	return text;
}


// 0 to 9999, without the zeros before it: 一千零一, 一百.
static std::string
Section(int value, const char* const* names, const char* const* units)
{
	std::string text;
	bool zero = false;
	for (int position = 3; position >= 0; position--) {
		int power = position == 3 ? 1000 : position == 2 ? 100
			: position == 1 ? 10 : 1;
		int digit = value / power % 10;
		if (digit == 0) {
			zero = !text.empty();
			continue;
		}
		if (zero)
			text += names[0];
		zero = false;
		text += names[digit];
		text += units[position];
	}
	return text;
}


std::string
ChineseNumber(const std::string& digits, bool upper)
{
	if (digits.empty())
		return std::string();
	for (char c : digits) {
		if (!IsDigit(c))
			return std::string();
	}
	size_t start = digits.find_first_not_of('0');
	if (start == std::string::npos)
		return upper ? kUpper[0] : kLower[0];
	std::string number = digits.substr(start);
	if (number.size() > 16)
		return std::string();

	static const char* const kLowerUnits[] = {"", "十", "百", "千"};
	static const char* const kUpperUnits[] = {"", "拾", "佰", "仟"};
	static const char* const kGroups[] = {"", "万", "亿", "万亿"};
	const char* const* names = upper ? kUpper : kLower;
	const char* const* units = upper ? kUpperUnits : kLowerUnits;

	// Groups of four from the right: 1,2345,6789 -> 一亿 二千三百四十五万 …
	std::vector<int> groups;
	for (size_t end = number.size(); end > 0; end -= std::min<size_t>(end, 4)) {
		size_t begin = end > 4 ? end - 4 : 0;
		groups.push_back(atoi(number.substr(begin, end - begin).c_str()));
	}
	std::string text;
	bool zero = false;
	for (size_t i = groups.size(); i-- > 0;) {
		int value = groups[i];
		if (value == 0) {
			zero = !text.empty();
			continue;
		}
		if (!text.empty() && (zero || value < 1000))
			text += names[0];
		zero = false;
		text += Section(value, names, units);
		text += kGroups[i];
	}
	// 一十二 is said 十二.
	static const std::string kOneTen = "一十";
	if (!upper && text.compare(0, kOneTen.size(), kOneTen) == 0)
		text.erase(0, std::string("一").size());
	return text;
}


// The digits after a decimal point, one by one: 点零五.
static std::string
Fraction(const std::string& digits, bool upper)
{
	std::string text = "点";
	for (char c : digits)
		text += (upper ? kUpper : kLower)[c - '0'];
	return text;
}


// 壹佰贰拾叁元整, 壹拾贰元伍角, 壹拾贰元零伍分.
static std::string
Money(const std::string& whole, const std::string& fraction)
{
	if (fraction.size() > 2)
		return std::string();
	int jiao = fraction.size() > 0 ? fraction[0] - '0' : 0;
	int fen = fraction.size() > 1 ? fraction[1] - '0' : 0;
	std::string yuan = ChineseNumber(whole, true);
	if (yuan.empty())
		return yuan;
	bool none = yuan == kUpper[0];
	std::string text = none && (jiao || fen) ? std::string() : yuan + "元";
	if (jiao == 0 && fen == 0)
		return text + "整";
	if (jiao != 0)
		text += std::string(kUpper[jiao]) + "角";
	else if (!none)
		text += kUpper[0];
	if (fen != 0)
		text += std::string(kUpper[fen]) + "分";
	return text;
}


static std::string
Roman(int value)
{
	static const struct { int value; const char* text; } kNumerals[] = {
		{1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"},
		{90, "XC"}, {50, "L"}, {40, "XL"}, {10, "X"}, {9, "IX"}, {5, "V"},
		{4, "IV"}, {1, "I"}};
	std::string text;
	for (const auto& numeral : kNumerals) {
		for (; value >= numeral.value; value -= numeral.value)
			text += numeral.text;
	}
	return text;
}


// ① to ㊿.
static std::string
Circled(int value)
{
	char32_t c = value <= 20 ? 0x2460 + value - 1
		: value <= 35 ? 0x3251 + value - 21 : 0x32b1 + value - 36;
	std::string text;
	text += char(0xe0 | (c >> 12));
	text += char(0x80 | ((c >> 6) & 0x3f));
	text += char(0x80 | (c & 0x3f));
	return text;
}


static std::string
Grouped(const std::string& digits)
{
	std::string text;
	for (size_t i = 0; i < digits.size(); i++) {
		if (i > 0 && (digits.size() - i) % 3 == 0)
			text += ',';
		text += digits[i];
	}
	return text;
}


// #pragma mark - arithmetic


namespace {

// + - * / ( ) and a % after a number, over doubles.
class Expression {
public:
	explicit Expression(const std::string& text) : fText(text) {}

	bool Evaluate(double* value)
	{
		if (!_Sum(value) || fAt != fText.size())
			return false;
		return std::isfinite(*value);
	}

private:
	bool _Sum(double* value)
	{
		if (!_Product(value))
			return false;
		while (fAt < fText.size() && (fText[fAt] == '+' || fText[fAt] == '-')) {
			char op = fText[fAt++];
			double right;
			if (!_Product(&right))
				return false;
			*value = op == '+' ? *value + right : *value - right;
		}
		return true;
	}

	bool _Product(double* value)
	{
		if (!_Factor(value))
			return false;
		while (fAt < fText.size() && (fText[fAt] == '*' || fText[fAt] == '/')) {
			char op = fText[fAt++];
			double right;
			if (!_Factor(&right))
				return false;
			if (op == '/' && right == 0)
				return false;
			*value = op == '*' ? *value * right : *value / right;
		}
		return true;
	}

	bool _Factor(double* value)
	{
		if (fAt >= fText.size())
			return false;
		if (fText[fAt] == '-' || fText[fAt] == '+') {
			bool negative = fText[fAt++] == '-';
			if (!_Factor(value))
				return false;
			if (negative)
				*value = -*value;
			return true;
		}
		if (fText[fAt] == '(') {
			fAt++;
			if (!_Sum(value) || fAt >= fText.size() || fText[fAt] != ')')
				return false;
			fAt++;
		} else {
			size_t start = fAt;
			while (fAt < fText.size()
				&& (IsDigit(fText[fAt]) || fText[fAt] == '.')) {
				fAt++;
			}
			if (fAt == start)
				return false;
			std::string number = fText.substr(start, fAt - start);
			if (number.find('.') != number.rfind('.'))
				return false;
			*value = strtod(number.c_str(), nullptr);
		}
		while (fAt < fText.size() && fText[fAt] == '%') {
			*value /= 100;
			fAt++;
		}
		return true;
	}

	const std::string& fText;
	size_t fAt = 0;
};

}	// namespace


static std::string
Format(double value)
{
	char text[64];
	if (std::fabs(value) < 1e15 && std::fabs(value - std::round(value)) < 1e-9)
		snprintf(text, sizeof(text), "%.0f", std::round(value));
	else
		snprintf(text, sizeof(text), "%.10g", value);
	std::string result = text;
	return result == "-0" ? "0" : result;
}


// #pragma mark - dates


static std::string
Weekday(int year, int month, int day)
{
	tm date = {};
	date.tm_year = year - 1900;
	date.tm_mon = month - 1;
	date.tm_mday = day;
	date.tm_hour = 12;
	if (mktime(&date) == time_t(-1))
		return std::string();
	return std::string("星期") + kWeekdays[date.tm_wday];
}


static std::vector<std::string>
DateForms(int year, int month, int day, bool weekday)
{
	std::string y = std::to_string(year);
	std::string m = std::to_string(month);
	std::string d = std::to_string(day);
	std::vector<std::string> forms = {
		y + "年" + m + "月" + d + "日",
		DigitByDigit(y) + "年" + ChineseNumber(m, false) + "月"
			+ ChineseNumber(d, false) + "日",
		y + "-" + TwoDigits(month) + "-" + TwoDigits(day),
	};
	if (weekday) {
		std::string name = Weekday(year, month, day);
		if (!name.empty())
			forms.push_back(forms[0] + " " + name);
	}
	return forms;
}


// 2026.11.7, 2026-11-7 or 2026/11/7; 2026.11 is a month.
static bool
ParseDate(const std::string& body, std::vector<std::string>* forms)
{
	char separator = 0;
	std::vector<std::string> parts(1);
	for (char c : body) {
		if (IsDigit(c)) {
			parts.back() += c;
			continue;
		}
		if ((c != '.' && c != '-' && c != '/') || (separator && c != separator))
			return false;
		separator = c;
		parts.emplace_back();
	}
	if (parts.size() < 2 || parts.size() > 3 || parts[0].size() != 4)
		return false;
	for (const std::string& part : parts) {
		if (part.empty() || part.size() > 4)
			return false;
	}
	int year = atoi(parts[0].c_str());
	int month = atoi(parts[1].c_str());
	if (month < 1 || month > 12 || parts[1].size() > 2)
		return false;
	if (parts.size() == 2) {
		// A year and a month: 2026.11 also reads as a number.
		if (separator == '.')
			return false;
		std::string y = parts[0];
		*forms = {y + "年" + std::to_string(month) + "月",
			DigitByDigit(y) + "年" + ChineseNumber(parts[1], false) + "月",
			y + "-" + TwoDigits(month)};
		return true;
	}
	int day = atoi(parts[2].c_str());
	static const int kDays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (day < 1 || day > kDays[month - 1] || parts[2].size() > 2)
		return false;
	*forms = DateForms(year, month, day, true);
	return true;
}


// #pragma mark - numbers


static std::vector<std::string>
NumberForms(const std::string& body)
{
	std::vector<std::string> forms;
	size_t point = body.find('.');
	std::string whole = body.substr(0, point);
	std::string fraction = point == std::string::npos ? std::string()
		: body.substr(point + 1);
	if (whole.empty())
		whole = "0";
	auto add = [&forms](const std::string& form) {
		if (form.empty())
			return;
		for (const std::string& shown : forms) {
			if (shown == form)
				return;
		}
		forms.push_back(form);
	};

	bool leadingZero = whole.size() > 1 && whole[0] == '0';
	if (leadingZero && point == std::string::npos)
		add(DigitByDigit(whole));
	std::string lower = ChineseNumber(whole, false);
	std::string upper = ChineseNumber(whole, true);
	if (lower.empty()) {
		// Too long to say: digit by digit.
		add(DigitByDigit(whole));
		return forms;
	}
	if (point != std::string::npos && !fraction.empty()) {
		add(lower + Fraction(fraction, false));
		add(upper + Fraction(fraction, true));
		add(Money(whole, fraction));
		return forms;
	}
	add(lower);
	add(upper);
	add(Money(whole, ""));
	if (whole.size() > 1)
		add(DigitByDigit(whole));
	if (whole.size() >= 5)
		add(Grouped(whole.substr(whole.find_first_not_of('0'))));
	long value = whole.size() <= 6 ? atol(whole.c_str()) : 0;
	if (value >= 1 && value <= 50)
		add(Circled(int(value)));
	if (value >= 1 && value < 4000)
		add(Roman(int(value)));
	return forms;
}


static std::vector<std::string>
Split(const char* symbols)
{
	std::vector<std::string> list;
	std::istringstream stream(symbols);
	std::string symbol;
	while (stream >> symbol)
		list.push_back(symbol);
	return list;
}


static std::vector<std::string>
CodeCandidates(const std::string& code, time_t now)
{
	tm local = {};
	localtime_r(&now, &local);
	int year = local.tm_year + 1900;
	int month = local.tm_mon + 1;
	int day = local.tm_mday;
	if (code == "rq") {
		std::vector<std::string> forms = DateForms(year, month, day, false);
		forms.push_back(std::to_string(month) + "月" + std::to_string(day) + "日");
		forms.push_back(std::to_string(year) + "/" + TwoDigits(month) + "/"
			+ TwoDigits(day));
		return forms;
	}
	if (code == "sj") {
		int hour = local.tm_hour;
		std::string hh = TwoDigits(hour);
		std::string mm = TwoDigits(local.tm_min);
		int twelve = hour % 12 == 0 ? 12 : hour % 12;
		return {hh + ":" + mm, hh + ":" + mm + ":" + TwoDigits(local.tm_sec),
			std::to_string(hour) + "时" + std::to_string(local.tm_min) + "分",
			std::string(hour < 12 ? "上午" : "下午") + std::to_string(twelve)
				+ "点" + std::to_string(local.tm_min) + "分",
			std::to_string(year) + "-" + TwoDigits(month) + "-"
				+ TwoDigits(day) + " " + hh + ":" + mm};
	}
	if (code == "xq") {
		const char* name = kWeekdays[local.tm_wday];
		return {std::string("星期") + name, std::string("周") + name,
			std::string("礼拜") + name};
	}
	for (const auto& group : kSymbols) {
		if (code == group.code)
			return Split(group.symbols);
	}
	return {};
}


std::vector<std::string>
VCandidates(const std::string& body, time_t now)
{
	if (VNumeric(body)) {
		std::vector<std::string> forms;
		if (ParseDate(body, &forms))
			return forms;
		bool plain = true;
		for (char c : body)
			plain = plain && (IsDigit(c) || c == '.');
		if (plain && body.find('.') == body.rfind('.') && body.back() != '.')
			return NumberForms(body);
		double value;
		if (!Expression(body).Evaluate(&value))
			return {};
		std::string result = Format(value);
		return {result, body + "=" + result};
	}
	for (char c : body) {
		if (c < 'a' || c > 'z')
			return {};
	}
	// A whole code: all it has. Part of one: the first of each it could be.
	std::vector<std::string> candidates;
	for (const VCode& code : VCodes()) {
		if (body == code.code)
			return CodeCandidates(code.code, now);
		if (std::string(code.code).compare(0, body.size(), body) == 0)
			candidates.push_back(CodeCandidates(code.code, now)[0]);
	}
	return candidates;
}

}	// namespace haikupinyin
