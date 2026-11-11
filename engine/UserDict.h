// Haiku Pinyin: words the user has picked, and phrases they've built by picking
// several candidates in a row. Picked words rank higher the more they're used.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Lexicon.h"

namespace haikupinyin {

class UserDict : public KeyIndex {
public:
	struct Word {
		std::vector<SyllableId> key;
		std::string text;
		float cost;		// what the lexicon gave it when first learned
		uint32_t count;
		uint64_t lastUsed;	// Time(): when it was last learned
	};
	// A word forgotten (Ctrl+Delete), kept so a copy from before that
	// (another machine's, an older file) doesn't bring it back.
	struct Forgotten {
		std::vector<SyllableId> key;
		std::string text;
		uint64_t time;
	};

	uint32_t Size() const override { return uint32_t(fWords.size()); }
	int KeyLength(uint32_t index) const override
		{ return int(fWords[index].key.size()); }
	SyllableId KeyAt(uint32_t index, int position) const override
		{ return fWords[index].key[position]; }

	const Word& At(uint32_t index) const { return fWords[index]; }
	float Cost(uint32_t index) const;
	const std::vector<Forgotten>& ForgottenWords() const { return fForgotten; }

	void Learn(const std::vector<SyllableId>& key, const std::string& text,
		float cost);
	bool Forget(const std::vector<SyllableId>& key, const std::string& text);
	void Clear() { fWords.clear(); fForgotten.clear(); fDirty = true; }
	// A word from elsewhere (an import): the higher count and the lower
	// cost win. True when it changed anything.
	bool Merge(const std::vector<SyllableId>& key, const std::string& text,
		float cost, uint32_t count);
	// Another copy's words and forgotten words (the file changed on disk,
	// another machine's): per word the higher count, the newer time and the
	// lower cost; a word forgotten at or after the time it was last learned
	// stays forgotten. The same whatever the order, and joining a copy twice
	// changes nothing. Answers how many entries changed here.
	int Join(const UserDict& other);
	// An entry as another copy has it, for building one to join; count 0 is
	// a forgotten word.
	void Add(const std::vector<SyllableId>& key, const std::string& text,
		float cost, uint32_t count, uint64_t time);

	// Seconds since 1970, and never less than any time it has seen plus
	// one, so learning a word after taking in a forget from a machine whose
	// clock runs ahead still counts as after. SetTime fixes now, for tests.
	uint64_t Time();
	void SetTime(uint64_t now) { fNow = now; }

	// Saved by spelling, so it survives a rebuilt lexicon.
	bool Load(const std::string& path, const Syllables& syllables);
	bool Save(const std::string& path, const Syllables& syllables);
	bool Dirty() const { return fDirty; }

private:
	std::vector<Word>::iterator _Find(const std::vector<SyllableId>& key,
		const std::string& text);
	std::vector<Forgotten>::iterator _FindForgotten(
		const std::vector<SyllableId>& key, const std::string& text);

	std::vector<Word> fWords;			// by key, then text
	std::vector<Forgotten> fForgotten;	// the same
	uint64_t fClock = 0;
	uint64_t fNow = 0;
	bool fDirty = false;
};

}	// namespace haikupinyin
