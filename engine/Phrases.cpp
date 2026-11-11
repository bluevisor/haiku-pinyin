#include "Phrases.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <sys/stat.h>

namespace haikupinyin {

static const size_t kLongestCode = 64;	// the engine's longest input


FileStamp
FileStamp::Of(const std::string& path)
{
	FileStamp stamp;
	struct stat info;
	if (path.empty() || stat(path.c_str(), &info) != 0)
		return stamp;
	stamp.seconds = int64_t(info.st_mtime);
#if defined(__APPLE__)
	stamp.nanoseconds = int64_t(info.st_mtimespec.tv_nsec);
#else
	stamp.nanoseconds = int64_t(info.st_mtim.tv_nsec);
#endif
	stamp.size = int64_t(info.st_size);
	stamp.inode = uint64_t(info.st_ino);
	return stamp;
}


bool
FileStamp::operator==(const FileStamp& other) const
{
	return seconds == other.seconds && nanoseconds == other.nanoseconds
		&& size == other.size && inode == other.inode;
}


static void
AppendUtf8(std::string& out, char32_t c)
{
	if (c < 0x80)
		out += char(c);
	else if (c < 0x800) {
		out += char(0xc0 | (c >> 6));
		out += char(0x80 | (c & 0x3f));
	} else if (c < 0x10000) {
		out += char(0xe0 | (c >> 12));
		out += char(0x80 | ((c >> 6) & 0x3f));
		out += char(0x80 | (c & 0x3f));
	} else {
		out += char(0xf0 | (c >> 18));
		out += char(0x80 | ((c >> 12) & 0x3f));
		out += char(0x80 | ((c >> 6) & 0x3f));
		out += char(0x80 | (c & 0x3f));
	}
}


std::string
DecodeText(const std::string& bytes)
{
	auto starts = [&](const char* bom, size_t length) {
		return bytes.size() >= length && bytes.compare(0, length, bom) == 0;
	};
	if (starts("\xef\xbb\xbf", 3))
		return bytes.substr(3);
	bool little = starts("\xff\xfe", 2);
	if (!little && !starts("\xfe\xff", 2))
		return bytes;
	// UTF-16, as Windows input methods write their files.
	std::string out;
	out.reserve(bytes.size());
	auto unit = [&](size_t at) {
		unsigned char a = bytes[at], b = bytes[at + 1];
		return char16_t(little ? (b << 8 | a) : (a << 8 | b));
	};
	for (size_t at = 2; at + 1 < bytes.size(); at += 2) {
		char32_t c = unit(at);
		if (c >= 0xd800 && c < 0xdc00 && at + 3 < bytes.size()) {
			char32_t low = unit(at + 2);
			if (low >= 0xdc00 && low < 0xe000) {
				c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
				at += 2;
			}
		}
		AppendUtf8(out, c);
	}
	return out;
}


// #pragma mark - Phrases


void
Phrases::SetPath(const std::string& path)
{
	fPath = path;
	fStamp = FileStamp();
	fPhrases.clear();
	Refresh();
}


bool
Phrases::Refresh()
{
	FileStamp stamp = FileStamp::Of(fPath);
	if (stamp == fStamp)
		return false;
	fStamp = stamp;
	std::ifstream file(fPath, std::ios::binary);
	std::ostringstream contents;
	if (file)
		contents << file.rdbuf();
	Parse(contents.str());
	return true;
}


bool
Phrases::ValidCode(const std::string& code)
{
	if (code.empty() || code.size() > kLongestCode || code[0] < 'a'
		|| code[0] > 'z') {
		return false;
	}
	for (char c : code) {
		if (!((c >= 'a' && c <= 'z') || c == '\'' || c == ';'))
			return false;
	}
	return true;
}


static std::string
Lower(std::string text)
{
	for (char& c : text) {
		if (c >= 'A' && c <= 'Z')
			c = char(c + 'a' - 'A');
	}
	return text;
}


// "code,position=text": Sogou's PhraseEdit.txt. False when it isn't one.
static bool
ParseSogou(const std::string& line, Phrases::Phrase& phrase)
{
	size_t comma = line.find(',');
	size_t equals = line.find('=');
	if (comma == std::string::npos || equals == std::string::npos
		|| equals < comma + 2) {
		return false;
	}
	std::string position = line.substr(comma + 1, equals - comma - 1);
	if (position.find_first_not_of("0123456789") != std::string::npos
		|| position.size() > 4) {
		return false;
	}
	phrase.code = Lower(line.substr(0, comma));
	phrase.position = std::max(1, atoi(position.c_str()));
	phrase.text = line.substr(equals + 1);
	return Phrases::ValidCode(phrase.code);
}


// "text<Tab>code<Tab>weight", the weight optional: Rime's custom_phrase.txt.
static bool
ParseRime(const std::string& line, Phrases::Phrase& phrase)
{
	size_t tab = line.find('\t');
	if (tab == std::string::npos || tab == 0)
		return false;
	size_t next = line.find('\t', tab + 1);
	phrase.text = line.substr(0, tab);
	phrase.code = Lower(line.substr(tab + 1,
		next == std::string::npos ? std::string::npos : next - tab - 1));
	phrase.position = 1;
	phrase.weight = next == std::string::npos ? 0
		: atoi(line.c_str() + next + 1);
	return Phrases::ValidCode(phrase.code);
}


void
Phrases::Parse(const std::string& bytes)
{
	fPhrases.clear();
	std::istringstream lines(DecodeText(bytes));
	std::string line;
	while (std::getline(lines, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty() || line[0] == '#' || line[0] == ';')
			continue;
		Phrase phrase;
		bool tabbed = line.find('\t') != std::string::npos;
		if (tabbed ? ParseRime(line, phrase) : ParseSogou(line, phrase)) {
			if (!phrase.text.empty())
				fPhrases.push_back(std::move(phrase));
		}
	}
	std::stable_sort(fPhrases.begin(), fPhrases.end(),
		[](const Phrase& a, const Phrase& b) {
			if (a.code != b.code)
				return a.code < b.code;
			if (a.position != b.position)
				return a.position < b.position;
			return a.weight > b.weight;
		});
}


std::vector<Phrases::Phrase>
Phrases::Find(const std::string& code) const
{
	auto at = std::lower_bound(fPhrases.begin(), fPhrases.end(), code,
		[](const Phrase& phrase, const std::string& code) {
			return phrase.code < code;
		});
	std::vector<Phrase> found;
	for (; at != fPhrases.end() && at->code == code; at++)
		found.push_back(*at);
	return found;
}


const char*
Phrases::Example()
{
	return
		"# 海酷拼音 自定义短语\n"
		"#\n"
		"# 每行一条：编码,位置=短语。打出编码（只能是字母）时，短语排在候选的\n"
		"# 那个位置上；一个编码可以有几条。以 # 或 ; 开头的行不算。存好就生效。\n"
		"#   dz,1=北京市海淀区中关村大街 1 号\n"
		"#   yx,1=me@example.com\n"
		"#   yx,2=work@example.com\n"
		"#\n"
		"# Rime 的 custom_phrase.txt 也能直接用：短语<Tab>编码<Tab>权重，\n"
		"# 排在第一位，权重大的在前。\n"
		"#\n"
		"# Custom phrases: one per line, code,position=text. When the keys typed\n"
		"# are exactly the code (letters only), the text is offered at that\n"
		"# position among the candidates. Rime's custom_phrase.txt lines work\n"
		"# too. Takes effect when saved.\n"
		"\n";
}


bool
Phrases::CreateFile() const
{
	if (fPath.empty())
		return false;
	if (FileStamp::Of(fPath).size >= 0)
		return true;
	std::ofstream file(fPath);
	file << Example();
	return bool(file.flush());
}


bool
Phrases::Append(const std::vector<Phrase>& phrases)
{
	if (fPath.empty() || !CreateFile())
		return false;
	std::string bytes;
	{
		std::ifstream file(fPath, std::ios::binary);
		std::ostringstream contents;
		contents << file.rdbuf();
		bytes = contents.str();
	}
	// A file in UTF-16 (Sogou's, copied in) becomes UTF-8 first.
	std::string text = DecodeText(bytes);
	bool rewrite = text.size() != bytes.size();
	std::ofstream file(fPath, rewrite ? std::ios::trunc : std::ios::app);
	if (rewrite)
		file << text;
	if (!text.empty() && text.back() != '\n')
		file << '\n';
	for (const Phrase& phrase : phrases) {
		file << phrase.code << ',' << phrase.position << '=' << phrase.text
			<< '\n';
	}
	if (!file.flush())
		return false;
	file.close();
	Refresh();
	return true;
}

}	// namespace haikupinyin
