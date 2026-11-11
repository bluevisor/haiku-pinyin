// Haiku Pinyin: the words it learned out to a text file and back in, and
// other input methods' word lists and phrases in.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Lexicon.h"
#include "Phrases.h"
#include "UserDict.h"

namespace haikupinyin {

// Why a file couldn't be imported at all.
enum class ImportProblem {
	kNone,
	kUnreadable,
	kNotUtf8,		// GBK, say: iconv -f GBK -t UTF-8 first
	kBinaryPlist,	// plutil -convert xml1 first
	kNotSaved,		// the user dictionary couldn't be written
};

// A file to import, read but not yet looked up in the lexicon.
struct WordFile {
	struct Word {
		std::string text;
		std::vector<std::string> pinyin;	// syllables; empty: unknown
		uint32_t count = 1;
	};

	std::string format;		// what it was read as, "" when nothing
	std::vector<Word> words;
	std::vector<Phrases::Phrase> phrases;
	int skipped = 0;		// entries that were there but unusable
	ImportProblem problem = ImportProblem::kNone;
};

// Reads ours (词<Tab>pin yin<Tab>count), Rime's user dictionary snapshots
// (*.userdb.txt) and exports, Sogou's exported user words and custom
// phrases, Rime's custom_phrase.txt, and the Mac's text replacements
// (the plist dragged out of System Settings). A tab-separated line whose
// code isn't a syllable per character is a Rime custom phrase.
WordFile ReadWordFile(const std::string& bytes, const Syllables& syllables);

// The words with their keys: the pinyin given, when it's all syllables,
// or else what the lexicon spells the text with. Words neither gives go
// uncounted into skipped.
struct ResolvedWord {
	std::vector<SyllableId> key;
	std::string text;
	float cost;
	uint32_t count;
};
std::vector<ResolvedWord> ResolveWords(const Lexicon& lexicon,
	const std::vector<WordFile::Word>& words, int* skipped);

// Ours: a comment, then 词<Tab>pin yin<Tab>count, most used first.
bool WriteWordFile(const std::string& path, const UserDict& user,
	const Syllables& syllables, int* count = nullptr);

}	// namespace haikupinyin
