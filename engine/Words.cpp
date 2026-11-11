#include "Words.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace haikupinyin {

static const size_t kLongestWord = 8;		// characters, when spelling text
static const float kSyllableCost = 8.0f;	// a syllable the lexicon lacks


// #pragma mark - reading


static std::vector<std::string>
Lines(const std::string& text)
{
	std::vector<std::string> lines;
	std::istringstream stream(text);
	std::string line;
	while (std::getline(stream, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		lines.push_back(std::move(line));
	}
	return lines;
}


static std::vector<std::string>
Split(const std::string& text, char separator)
{
	std::vector<std::string> parts;
	std::string part;
	std::istringstream stream(text);
	while (std::getline(stream, part, separator)) {
		if (!part.empty())
			parts.push_back(part);
	}
	return parts;
}


static bool
StartsWith(const std::string& text, const char* prefix)
{
	return text.rfind(prefix, 0) == 0;
}


static uint32_t
Count(const std::string& text)
{
	long count = strtol(text.c_str(), nullptr, 10);
	return count > 0 ? uint32_t(std::min(count, 1000000L)) : 0;
}


static size_t
CharacterCount(const std::string& text)
{
	size_t count = 0;
	for (unsigned char c : text)
		count += (c & 0xc0) != 0x80;
	return count;
}


// &amp; &lt; &#x4e2d; and the rest, in a plist's strings.
static std::string
XmlText(const std::string& text)
{
	static const struct { const char* name; const char* text; } kNamed[] = {
		{"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"},
	};
	std::string out;
	for (size_t i = 0; i < text.size(); i++) {
		size_t end = text[i] == '&' ? text.find(';', i) : std::string::npos;
		if (end == std::string::npos || end - i > 10) {
			out += text[i];
			continue;
		}
		std::string name = text.substr(i + 1, end - i - 1);
		bool known = false;
		for (const auto& named : kNamed) {
			if (name == named.name) {
				out += named.text;
				known = true;
			}
		}
		if (!known && name.size() > 1 && name[0] == '#') {
			bool hex = name[1] == 'x' || name[1] == 'X';
			unsigned long c = strtoul(name.c_str() + (hex ? 2 : 1), nullptr,
				hex ? 16 : 10);
			if (c > 0 && c < 0x110000) {
				AppendUtf8(out, char32_t(c));
				known = true;
			}
		}
		if (known)
			i = end;
		else
			out += text[i];
	}
	return out;
}


// The Mac's text replacements, dragged out of System Settings: a plist
// array of dicts, each with a phrase and a shortcut.
static void
ReadPlist(const std::string& text, WordFile& file)
{
	file.format = "text replacements";
	size_t at = 0;
	while ((at = text.find("<dict>", at)) != std::string::npos) {
		size_t end = text.find("</dict>", at);
		if (end == std::string::npos)
			break;
		std::string dict = text.substr(at, end - at);
		at = end;
		std::string phrase, shortcut, key;
		size_t position = 0;
		while (true) {
			size_t open = dict.find('<', position);
			if (open == std::string::npos)
				break;
			size_t close = dict.find('>', open);
			if (close == std::string::npos)
				break;
			std::string tag = dict.substr(open + 1, close - open - 1);
			position = close + 1;
			if (tag != "key" && tag != "string")
				continue;
			size_t finish = dict.find("</" + tag + ">", position);
			if (finish == std::string::npos)
				break;
			std::string value = XmlText(dict.substr(position, finish - position));
			position = finish;
			if (tag == "key")
				key = value;
			else if (key == "phrase")
				phrase = value;
			else if (key == "shortcut")
				shortcut = value;
		}
		Phrases::Phrase entry;
		for (char c : shortcut)
			entry.code += c >= 'A' && c <= 'Z' ? char(c + 'a' - 'A') : c;
		entry.text = phrase;
		if (Phrases::ValidCode(entry.code) && !phrase.empty()) {
			file.phrases.push_back(entry);
		} else
			file.skipped++;
	}
}


static bool
ValidUtf8(const std::string& text)
{
	for (size_t i = 0; i < text.size();) {
		unsigned char c = text[i];
		size_t length = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2
			: (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
		if (length == 0 || i + length > text.size())
			return false;
		for (size_t k = 1; k < length; k++) {
			if ((static_cast<unsigned char>(text[i + k]) & 0xc0) != 0x80)
				return false;
		}
		i += length;
	}
	return true;
}


static int FindSyllable(const Syllables& syllables, std::string name);


// Pinyin a syllable per character, every one known.
static bool
Spells(const Syllables& syllables, const std::vector<std::string>& pinyin,
	const std::string& text)
{
	if (pinyin.empty() || pinyin.size() != CharacterCount(text))
		return false;
	for (const std::string& name : pinyin) {
		if (FindSyllable(syllables, name) < 0)
			return false;
	}
	return true;
}


WordFile
ReadWordFile(const std::string& bytes, const Syllables& syllables)
{
	WordFile file;
	if (StartsWith(bytes, "bplist")) {
		file.problem = ImportProblem::kBinaryPlist;
		return file;
	}
	std::string text = DecodeText(bytes);
	if (!ValidUtf8(text)) {
		file.problem = ImportProblem::kNotUtf8;
		return file;
	}
	if (text.find("<plist") != std::string::npos) {
		ReadPlist(text, file);
		return file;
	}
	std::vector<std::string> lines = Lines(text);

	// Sogou's custom phrases: lines of code,position=text, some of them
	// taking the lines after.
	for (const std::string& line : lines) {
		Phrases::Phrase phrase;
		if (line.find('\t') == std::string::npos
			&& Phrases::ParseSogou(line, phrase)) {
			file.phrases = Phrases::Read(text, &file.skipped);
			file.format = "Sogou custom phrases";
			return file;
		}
	}
	// Rime's custom_phrase.txt says so in its header: every line a phrase.
	bool phrases = false;
	for (const std::string& line : lines) {
		phrases = phrases || (StartsWith(line, "#@/db_name\t")
			&& line.find("custom_phrase") != std::string::npos);
	}

	std::string format;
	auto seen = [&](const char* name) {
		if (format.empty())
			format = name;
		else if (format != name)
			format = "mixed";
	};
	for (std::string& line : lines) {
		if (line.empty() || line[0] == '#' || line[0] == ';')
			continue;
		std::vector<std::string> fields;
		{
			std::string field;
			std::istringstream stream(line);
			while (std::getline(stream, field, '\t'))
				fields.push_back(field);
		}
		WordFile::Word word;
		if (fields.size() >= 3 && StartsWith(fields[2], "c=")) {
			// Rime's user dictionary snapshot: pin yin <Tab>词<Tab>c=3 d=0.9
			// t=42. A count below one is a word deleted (or never typed).
			word.pinyin = Split(fields[0], ' ');
			word.text = fields[1];
			word.count = Count(fields[2].substr(2));
			seen("Rime user dictionary");
		} else if (fields.size() >= 2 && !fields[0].empty()) {
			// 词<Tab>pin yin<Tab>count: ours, and Rime's export. A code that
			// isn't a syllable per character makes it a phrase: Rime's
			// custom_phrase.txt (text<Tab>code<Tab>weight).
			word.text = fields[0];
			word.pinyin = Split(fields[1], ' ');
			if (phrases || !Spells(syllables, word.pinyin, word.text)) {
				Phrases::Phrase phrase;
				for (char c : fields[1]) {
					if (c != ' ')
						phrase.code += c >= 'A' && c <= 'Z' ? char(c + 'a' - 'A') : c;
				}
				phrase.text = word.text;
				phrase.weight = fields.size() >= 3 ? atoi(fields[2].c_str()) : 0;
				if (Phrases::ValidCode(phrase.code)) {
					file.phrases.push_back(phrase);
					seen("Rime custom phrases");
				} else
					file.skipped++;
				continue;
			}
			// No count, or a weight below one: once.
			word.count = fields.size() >= 3 ? std::max(1u, Count(fields[2])) : 1;
			seen("Haiku Pinyin");
		} else if (line[0] == '\'') {
			// Sogou's exported words: 'pin'yin 词, no count (a number after
			// the word, as some tools add, is taken as one).
			size_t space = line.find(' ');
			if (space == std::string::npos) {
				file.skipped++;
				continue;
			}
			word.pinyin = Split(line.substr(0, space), '\'');
			std::vector<std::string> rest = Split(line.substr(space + 1), ' ');
			word.text = rest.empty() ? "" : rest[0];
			if (rest.size() > 1)
				word.count = std::max(1u, Count(rest[1]));
			seen("Sogou");
		} else {
			file.skipped++;
			continue;
		}
		if (word.text.empty() || word.count == 0) {
			file.skipped++;
			continue;
		}
		file.words.push_back(std::move(word));
	}
	file.format = format;
	return file;
}


// #pragma mark - resolving


static std::vector<std::string>
Characters(const std::string& text)
{
	std::vector<std::string> characters;
	for (size_t i = 0; i < text.size();) {
		size_t length = 1;
		unsigned char c = text[i];
		if (c >= 0xf0)
			length = 4;
		else if (c >= 0xe0)
			length = 3;
		else if (c >= 0xc0)
			length = 2;
		characters.push_back(text.substr(i, length));
		i += length;
	}
	return characters;
}


// Pinyin as the lexicon spells it: lowercase, ü as v, no tone numbers.
static int
FindSyllable(const Syllables& syllables, std::string name)
{
	std::string plain;
	for (size_t i = 0; i < name.size(); i++) {
		if (name.compare(i, 2, "\xc3\xbc") == 0) {	// ü
			plain += 'v';
			i++;
		} else if (name[i] >= 'A' && name[i] <= 'Z')
			plain += char(name[i] + 'a' - 'A');
		else if (name[i] < '1' || name[i] > '5')
			plain += name[i];
	}
	int id = syllables.Find(plain);
	// lue and nue, as some write them.
	if (id < 0 && plain.size() == 3 && (plain[0] == 'l' || plain[0] == 'n')
		&& plain.compare(1, 2, "ue") == 0) {
		id = syllables.Find(plain.substr(0, 1) + "ve");
	}
	return id;
}


// The lowest cost the lexicon gives text under exactly this key, or
// infinity.
static float
LexiconCost(const Lexicon& lexicon, const SyllableId* key, int length,
	std::string_view text)
{
	KeyIndex::Range range = lexicon.All();
	for (int depth = 0; depth < length && !range.Empty(); depth++)
		range = lexicon.Narrow(range, depth, key[depth]);
	range = lexicon.Exact(range, length);
	float best = std::numeric_limits<float>::infinity();
	for (uint32_t i = range.begin; i < range.end; i++) {
		if (lexicon.Word(i) == text)
			best = std::min(best, lexicon.Cost(i));
	}
	return best;
}


// A word with its pinyin: the cheapest way the lexicon splits it into words
// under that key, or a guess when the key and the text don't line up.
static float
CostWithKey(const Lexicon& lexicon, const std::vector<SyllableId>& key,
	const std::string& text)
{
	std::vector<std::string> characters = Characters(text);
	size_t length = characters.size();
	if (length != key.size())
		return kSyllableCost * key.size();
	float infinity = std::numeric_limits<float>::infinity();
	std::vector<float> best(length + 1, infinity);
	best[0] = 0;
	for (size_t from = 0; from < length; from++) {
		if (best[from] == infinity)
			continue;
		std::string piece;
		for (size_t to = from + 1; to <= length && to - from <= kLongestWord;
				to++) {
			piece += characters[to - 1];
			float cost = LexiconCost(lexicon, key.data() + from,
				int(to - from), piece);
			best[to] = std::min(best[to], best[from] + cost);
		}
	}
	if (best[length] == infinity)
		return kSyllableCost * key.size();
	return best[length];
}


std::vector<ResolvedWord>
ResolveWords(const Lexicon& lexicon, const std::vector<WordFile::Word>& words,
	int* skipped)
{
	const Syllables& syllables = lexicon.GetSyllables();
	std::vector<ResolvedWord> resolved;
	std::vector<const WordFile::Word*> unspelled;
	for (const WordFile::Word& word : words) {
		std::vector<SyllableId> key;
		if (word.pinyin.size() != CharacterCount(word.text)) {
			unspelled.push_back(&word);
			continue;
		}
		for (const std::string& name : word.pinyin) {
			int id = FindSyllable(syllables, name);
			if (id < 0) {
				key.clear();
				break;
			}
			key.push_back(SyllableId(id));
		}
		if (key.empty())
			unspelled.push_back(&word);
		else {
			resolved.push_back(ResolvedWord{key, word.text,
				CostWithKey(lexicon, key, word.text), word.count});
		}
	}
	if (unspelled.empty())
		return resolved;

	// The rest are spelled the way the lexicon spells their pieces: every
	// piece they could split into, looked up in one pass over it.
	struct Spelling {
		float cost = std::numeric_limits<float>::infinity();
		uint32_t index = 0;
	};
	std::unordered_map<std::string, Spelling> pieces;
	for (const WordFile::Word* word : unspelled) {
		std::vector<std::string> characters = Characters(word->text);
		for (size_t from = 0; from < characters.size(); from++) {
			std::string piece;
			for (size_t to = from; to < characters.size()
					&& to - from < kLongestWord; to++) {
				piece += characters[to];
				pieces[piece];
			}
		}
	}
	size_t longest = kLongestWord * 4;
	std::string text;
	for (uint32_t i = 0; i < lexicon.Size(); i++) {
		std::string_view word = lexicon.Word(i);
		if (word.size() > longest)
			continue;
		text.assign(word);
		auto found = pieces.find(text);
		if (found != pieces.end() && lexicon.Cost(i) < found->second.cost)
			found->second = Spelling{lexicon.Cost(i), i};
	}

	float infinity = std::numeric_limits<float>::infinity();
	for (const WordFile::Word* word : unspelled) {
		std::vector<std::string> characters = Characters(word->text);
		size_t length = characters.size();
		std::vector<float> best(length + 1, infinity);
		std::vector<size_t> from(length + 1, 0);
		std::vector<uint32_t> through(length + 1, 0);
		best[0] = 0;
		for (size_t start = 0; start < length; start++) {
			if (best[start] == infinity)
				continue;
			std::string piece;
			for (size_t end = start + 1; end <= length
					&& end - start <= kLongestWord; end++) {
				piece += characters[end - 1];
				const Spelling& spelling = pieces[piece];
				if (best[start] + spelling.cost < best[end]) {
					best[end] = best[start] + spelling.cost;
					from[end] = start;
					through[end] = spelling.index;
				}
			}
		}
		if (length == 0 || best[length] == infinity) {
			(*skipped)++;
			continue;
		}
		std::vector<std::vector<SyllableId>> parts;
		for (size_t end = length; end > 0; end = from[end]) {
			uint32_t index = through[end];
			std::vector<SyllableId> part;
			for (int depth = 0; depth < lexicon.KeyLength(index); depth++)
				part.push_back(lexicon.KeyAt(index, depth));
			parts.push_back(std::move(part));
		}
		std::vector<SyllableId> key;
		for (auto part = parts.rbegin(); part != parts.rend(); part++)
			key.insert(key.end(), part->begin(), part->end());
		resolved.push_back(ResolvedWord{key, word->text, best[length],
			word->count});
	}
	return resolved;
}


// #pragma mark - writing


bool
WriteWordFile(const std::string& path, const UserDict& user,
	const Syllables& syllables, int* count)
{
	std::vector<const UserDict::Word*> words;
	for (uint32_t i = 0; i < user.Size(); i++)
		words.push_back(&user.At(i));
	std::stable_sort(words.begin(), words.end(),
		[](const UserDict::Word* a, const UserDict::Word* b) {
			return a->count > b->count;
		});
	std::string temp = path + ".new";
	{
		std::ofstream file(temp, std::ios::trunc);
		if (!file)
			return false;
		file << "# 海酷拼音 学到的词：词<Tab>拼音<Tab>次数\n"
			"# Haiku Pinyin learned words: word<Tab>pinyin<Tab>count\n";
		for (const UserDict::Word* word : words) {
			file << word->text << '\t';
			for (size_t i = 0; i < word->key.size(); i++)
				file << (i > 0 ? " " : "") << syllables.Name(word->key[i]);
			file << '\t' << word->count << '\n';
		}
		if (!file.flush())
			return false;
	}
	if (rename(temp.c_str(), path.c_str()) != 0)
		return false;
	if (count != nullptr)
		*count = int(words.size());
	return true;
}

}	// namespace haikupinyin
