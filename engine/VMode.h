// Haiku Pinyin: v-mode, what v and then digits or a short code offer instead
// of pinyin (v in full pinyin, Shift+V in shuangpin, where v is a key).
//   v123        一百二十三, 壹佰贰拾叁, 壹佰贰拾叁元整, 一二三, …
//   v12.5       十二点五, 壹拾贰点伍, 壹拾贰元伍角
//   v1+2*3      7, 1+2*3=7
//   v2026.11.7  2026年11月7日, 二〇二六年十一月七日, 2026-11-07, …
//   vrq vsj vxq today's date, the time, the day of the week
//   vjt vsx …   symbols: arrows, maths, units, Greek, … (VCodes())
#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace haikupinyin {

// Whether what follows the v is a number (digits pick then aren't digits:
// the candidates are picked with letters).
bool VNumeric(const std::string& body);

// What body (the keys after the v) offers at time now. A code still being
// typed offers the first of each code it could become; nothing when it
// can't become one.
std::vector<std::string> VCandidates(const std::string& body, time_t now);

struct VCode {
	const char* code;
	const char* name;
};
std::vector<VCode> VCodes();

// Chinese numerals for a whole number of up to 16 digits: 一百二十三, or with
// upper, 壹佰贰拾叁. "" when it isn't one.
std::string ChineseNumber(const std::string& digits, bool upper);

}	// namespace haikupinyin
