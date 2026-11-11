// Haiku Pinyin: custom phrases (自定义短语), a text file the user keeps that
// maps a code to text (dz → an address, yx → an email), offered at a chosen
// place among the candidates when the keys typed are exactly the code.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace haikupinyin {

// Enough of a file's stat to tell that it changed.
struct FileStamp {
	int64_t seconds = 0;
	int64_t nanoseconds = 0;
	int64_t size = -1;		// -1: no file
	uint64_t inode = 0;

	static FileStamp Of(const std::string& path);
	bool operator==(const FileStamp& other) const;
	bool operator!=(const FileStamp& other) const { return !(*this == other); }
};

// Text in UTF-8, from UTF-8 (BOM or not) or UTF-16 with a BOM.
std::string DecodeText(const std::string& bytes);


class Phrases {
public:
	struct Phrase {
		std::string code;	// lowercase
		std::string text;
		int position = 1;	// among the candidates, from 1
		int weight = 0;		// Rime's: higher first among one position
	};

	// Reads the file again when it changed since the last look: a stat.
	// True when it was read.
	void SetPath(const std::string& path);
	const std::string& Path() const { return fPath; }
	bool Refresh();

	// The file's text: lines are "code,position=text" (Sogou's) or
	// "text<Tab>code<Tab>weight" (Rime's custom_phrase.txt, at position 1);
	// # and ; start comments; anything else is ignored.
	void Parse(const std::string& text);
	size_t Size() const { return fPhrases.size(); }

	// The phrases for exactly this code, by position.
	std::vector<Phrase> Find(const std::string& code) const;

	// A new file's text: how to write one, commented out.
	static const char* Example();
	// Writes Example() when there's no file yet.
	bool CreateFile() const;
	// Adds phrases at the end of the file, in its own syntax.
	bool Append(const std::vector<Phrase>& phrases);
	// Whether text can stand as a line's code: what typing can spell.
	static bool ValidCode(const std::string& code);

private:
	std::string fPath;
	FileStamp fStamp;
	std::vector<Phrase> fPhrases;	// by code, then position, then weight
};

}	// namespace haikupinyin
