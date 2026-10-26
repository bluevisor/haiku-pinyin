// Haiku Pinyin: the input method itself, independent of any windowing system.
// Feed it keys; it tells you what to show and what to commit.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Converter.h"
#include "Decoder.h"
#include "Lexicon.h"
#include "Spelling.h"
#include "UserDict.h"

namespace haikupinyin {

enum class Script {
	kSimplified,
	kTraditional,	// OpenCC's standard forms
	kTaiwan,
	kHongKong
};

struct Options {
	std::string layout = "full";	// or a shuangpin layout: pyjj, xiaohe,
									// ziranma, mspy, abc
	Script script = Script::kSimplified;
	int pageSize = 7;
	bool fullwidthPunctuation = true;
};

enum class Key {
	kCharacter,		// with the character
	kSpace,
	kReturn,
	kBackspace,
	kDelete,
	kEscape,
	kTab,
	kLeft,
	kRight,
	kUp,
	kDown,
	kPageUp,
	kPageDown,
	kHome,
	kEnd,
	kOther
};

enum Modifier {
	kShift = 1,
	kControl = 2,
	kCommand = 4,
	kOption = 8
};


class Engine {
public:
	Engine();

	// dataDirectory holds pinyin.dict, shuangpin.txt, s2t.txt, tw.txt and
	// hk.txt.
	bool Load(const std::string& dataDirectory, std::string* error = nullptr);
	bool Loaded() const { return fLexicon.Loaded(); }

	void SetUserDictionary(const std::string& path);
	bool SaveUserDictionary();
	UserDict& UserDictionary() { return fUser; }

	const Options& GetOptions() const { return fOptions; }
	void SetOptions(const Options& options);
	static std::vector<std::string> Layouts();

	bool Chinese() const { return fChinese; }
	void SetChinese(bool chinese);

	// True when the key was used. Whether or not it was, there may be text
	// to commit first (a punctuation key commits the sentence before it).
	bool ProcessKey(Key key, char32_t character, uint32_t modifiers);
	std::string TakeCommit();

	bool Composing() const { return !fKeys.empty(); }
	// Inline text: what's picked so far, then the spelled pinyin.
	std::string Preedit() const;
	// The part of Preedit() that is picked text (bytes).
	size_t PickedLength() const;
	std::string Spelled() const { return fSpelled; }

	// Candidates on the current page, in the chosen script.
	std::vector<std::string> PageCandidates() const;
	int Highlighted() const { return fHighlight % fOptions.pageSize; }
	int Page() const { return fHighlight / fOptions.pageSize; }
	bool HasPreviousPage() const { return Page() > 0; }
	bool HasNextPage() const;
	bool SelectOnPage(int index);

	void Reset();

private:
	struct Pick {
		std::string text;
		int consumed;
		std::vector<SyllableId> key;
		float cost;
	};

	bool _Compose(Key key, char32_t character);
	bool _IsLayoutKey(char32_t character) const;
	void _Update();
	void _Select(const Candidate& candidate);
	void _Finish(const std::string& tail);
	void _Move(int delta);
	std::string _Punctuation(char32_t character);
	std::string _Convert(const std::string& text) const;
	std::string _Remaining() const { return fKeys.substr(fUsed); }

	Lexicon fLexicon;
	Shuangpin fShuangpin;
	Converter fToTraditional;
	Converter fToTaiwan;
	Converter fToHongKong;
	UserDict fUser;
	std::string fUserPath;
	int fUnsaved = 0;
	Decoder fDecoder;
	SyllableGraph fGraph;

	Options fOptions;
	bool fChinese = true;

	std::string fKeys;			// everything typed in this composition
	size_t fUsed = 0;			// keys taken by picks
	std::vector<Pick> fPicks;
	std::vector<Candidate> fCandidates;
	std::string fSpelled;
	int fHighlight = 0;			// over all candidates

	std::string fCommit;
	char32_t fLastCharacter = 0;
	bool fDoubleQuoteOpen = false;
	bool fSingleQuoteOpen = false;
};

}	// namespace haikupinyin
