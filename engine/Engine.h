// Haiku Pinyin: the input method itself, independent of any windowing system.
// Feed it keys; it tells you what to show and what to commit.
#pragma once

#include <ctime>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

#include "Bigram.h"
#include "Converter.h"
#include "Decoder.h"
#include "Lexicon.h"
#include "Phrases.h"
#include "Spelling.h"
#include "Sync.h"
#include "UserDict.h"
#include "Words.h"

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
	bool emoji = true;				// 太阳 also offers 🌞
	unsigned fuzzy = 0;				// 模糊音, a Fuzzy mask
	bool english = true;			// "hello" also offers hello
	bool cloud = false;				// the front end asks a cloud service
	Vowels vowels = Vowels::kDoubled;	// shuangpin: aa oo ee, oa oo oe,
										// or a o e
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

	// Also sets the custom phrases file: phrases.txt in the same folder.
	void SetUserDictionary(const std::string& path);
	bool SaveUserDictionary();
	UserDict& UserDictionary() { return fUser; }

	// Custom phrases (Phrases.h), read again when the file changes, as a
	// composition starts.
	void SetPhrasesFile(const std::string& path);
	const std::string& PhrasesFile() const { return fPhrases.Path(); }
	Phrases& CustomPhrases() { return fPhrases; }

	// The learned words out to a text file (词<Tab>pin yin<Tab>count), and
	// words and phrases in from that or another input method's files
	// (Words.h): merged with what's there, the higher count winning, and
	// saved. Phrases go into the phrases file.
	struct ImportResult {
		std::string format;		// what the file was read as; "": nothing
		int words = 0;
		int phrases = 0;
		int skipped = 0;		// entries it couldn't use
		ImportProblem problem = ImportProblem::kNone;

		std::string Summary() const;	// in Chinese, for Linux and Haiku
	};
	bool ExportWords(const std::string& path, int* count = nullptr);
	ImportResult ImportWords(const std::string& path);

	// Sync (Sync.h) through a folder the user's own tool syncs, "" for off:
	// the sync-folder file beside user.dict says which, read again when it
	// changes, as a composition starts. SetSyncFolder sets it for this
	// engine alone, the file aside. Compositions start the worker that
	// reads and writes the folder; what it brings is joined in as the next
	// one starts, and saving the dictionary has it write ours.
	void SetSyncFolder(const std::string& folder);
	const std::string& SyncFolder() const { return fSync.Folder(); }
	// One round on this thread (hpcli sync): the others' files in, the
	// dictionary saved, ours out.
	SyncReport SyncNow();

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
	// In v-mode (VMode.h): v, or Shift+V in shuangpin, then a number or a
	// code.
	bool InVMode() const { return fVMode; }
	// The keys that pick the candidates on the page, in order: digits, or
	// letters while v-mode takes digits as typing.
	std::string PickKeys() const;
	// What v-mode takes as now (0, the default: the clock). For tests.
	void SetTime(time_t now) { fNow = now; }
	// Forgets the last word and the text before the cursor: the cursor
	// moved elsewhere.
	void ClearContext();
	// The text before the cursor, from a front end that can read it. Its
	// last word becomes the context, and the AI reads all of it. Without
	// one, the engine goes by what was committed and typed through since
	// the last ClearContext().
	void SetContextText(const std::string& text);
	const std::string& ContextText() const { return fContextText; }
	// Inline text: what's picked so far, then the spelled pinyin.
	std::string Preedit() const;
	// The part of Preedit() that is picked text (bytes).
	size_t PickedLength() const;
	std::string Spelled() const { return fSpelled; }

	// Candidates on the current page, in the chosen script; custom phrases
	// as written, on one line (PhraseLabel()).
	std::vector<std::string> PageCandidates() const;
	static std::string PhraseLabel(const std::string& text);
	int Highlighted() const { return fHighlight % fOptions.pageSize; }
	int Page() const { return fHighlight / fOptions.pageSize; }
	bool HasPreviousPage() const { return Page() > 0; }
	bool HasNextPage() const;
	bool SelectOnPage(int index);

	// Cloud candidates: the full pinyin to ask about ("" when nothing
	// worth asking), and the answer for it. True when the answer was added.
	std::string CloudQuery() const;
	bool SetCloudResult(const std::string& query, const std::string& text);
	// Where the cloud candidate sits on this page, or -1.
	int CloudIndexOnPage() const;

	// AI picks and completions, for a front end with a language model that
	// reads the text before the cursor. Like the cloud, it's asked on the
	// side, and an answer that no longer fits what's on screen is ignored.
	//
	// Picks: which of the candidates that spell all the keys reads best
	// after the prompt. No options when there's nothing to ask.
	struct AIQuery {
		std::string prompt;		// the text before the cursor, then the picks
		std::vector<std::string> options;	// simplified, as the lexicon has
		uint32_t revision = 0;	// of the candidates asked about
		std::vector<int> indexes;	// of the options among them
	};
	AIQuery AIPickQuery() const;
	// ln P(option | prompt) for each option. A clear winner moves first,
	// unless the candidates changed or the highlight moved since the query.
	// True when it moved.
	bool SetAIPickResult(const AIQuery& query, const std::vector<double>& scores);
	// Where the candidate the AI moved sits on this page, or -1.
	int AIIndexOnPage() const;
	// Completions: the text to continue ("" when nothing worth asking, as
	// after English or punctuation), and the model's guess at what follows.
	// True when it's taken: Suggestion() offers it until the next key, and
	// Tab commits it.
	std::string AICompletionQuery() const;
	bool SetAICompletion(const std::string& prompt, const std::string& text);
	std::string Suggestion() const;

	void Reset();

private:
	struct Pick {
		std::string text;
		int consumed;
		std::vector<SyllableId> key;
		float cost;
		std::string tail;
		bool phrase;
	};

	bool _ProcessKey(Key key, char32_t character, uint32_t modifiers);
	bool _Compose(Key key, char32_t character);
	// shown goes to the client; context, simplified, to ContextText().
	void _Commit(const std::string& shown, const std::string& context);
	void _AddContext(const std::string& text);
	bool _IsLayoutKey(char32_t character) const;
	bool _ComposeV(char32_t character, bool* used);
	void _Update();
	bool _UpdateV();
	void _AddEmoji();
	void _AddEnglish();
	void _PlacePhrases();
	void _Refresh();
	void _RefreshUser();
	void _RefreshSync();
	void _ConfigureSync();
	int _JoinSynced(const std::vector<SyncedWord>& words);
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
	std::unordered_map<std::string, std::vector<std::string>> fEmoji;
	UserDict fUser;
	std::string fUserPath;
	FileStamp fUserStamp;		// of the user dictionary, as last read or saved
	Phrases fPhrases;
	int fUnsaved = 0;
	Sync fSync;
	std::string fSyncFolder;
	FileStamp fSyncStamp;		// of the sync-folder file, as last read
	bool fSyncFixed = false;	// by SetSyncFolder
	Bigram fBigram;
	std::vector<std::string> fEnglish;		// sorted, ignoring case
	std::vector<std::string> fEnglishLower;
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
	bool fVMode = false;
	time_t fNow = 0;

	std::string fCommit;
	uint32_t fContext = 0;		// Bigram::Hash of the last committed word
	uint32_t fRevision = 0;		// of fCandidates
	std::string fContextText;	// before the cursor, the last few characters
	std::string fSuggestion;	// the AI's completion on offer
	char32_t fLastCharacter = 0;
	bool fDoubleQuoteOpen = false;
	bool fSingleQuoteOpen = false;
};

}	// namespace haikupinyin
