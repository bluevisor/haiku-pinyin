// Qingpin: words the user has picked, and phrases they've built by picking
// several candidates in a row. Picked words rank higher the more they're used.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Lexicon.h"

namespace qingpin {

class UserDict : public KeyIndex {
public:
	struct Word {
		std::vector<SyllableId> key;
		std::string text;
		float cost;		// what the lexicon gave it when first learned
		uint32_t count;
		uint64_t lastUsed;
	};

	uint32_t Size() const override { return uint32_t(fWords.size()); }
	int KeyLength(uint32_t index) const override
		{ return int(fWords[index].key.size()); }
	SyllableId KeyAt(uint32_t index, int position) const override
		{ return fWords[index].key[position]; }

	const Word& At(uint32_t index) const { return fWords[index]; }
	float Cost(uint32_t index) const;

	void Learn(const std::vector<SyllableId>& key, const std::string& text,
		float cost);
	bool Forget(const std::vector<SyllableId>& key, const std::string& text);
	void Clear() { fWords.clear(); fDirty = true; }

	// Saved by spelling, so it survives a rebuilt lexicon.
	bool Load(const std::string& path, const Syllables& syllables);
	bool Save(const std::string& path, const Syllables& syllables);
	bool Dirty() const { return fDirty; }

private:
	std::vector<Word>::iterator _Find(const std::vector<SyllableId>& key,
		const std::string& text);

	std::vector<Word> fWords;
	uint64_t fClock = 0;
	bool fDirty = false;
};

}	// namespace qingpin
