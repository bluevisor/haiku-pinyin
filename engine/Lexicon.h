// Qingpin: the compiled word list (build/data/qingpin.dict) and the syllable
// table that comes with it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace qingpin {

typedef uint16_t SyllableId;

class Syllables {
public:
	void Add(std::string name);

	int Count() const { return int(fNames.size()); }
	const std::string& Name(SyllableId id) const { return fNames[id]; }
	// -1 when it's not a syllable.
	int Find(std::string_view name) const;
	// Every syllable starting with prefix, sorted (empty when none).
	const std::vector<SyllableId>& WithPrefix(std::string_view prefix) const;
	int MaxLength() const { return fMaxLength; }

private:
	std::vector<std::string> fNames;
	std::unordered_map<std::string, SyllableId> fIds;
	std::unordered_map<std::string, std::vector<SyllableId>> fPrefixes;
	int fMaxLength = 0;
};


// Anything holding words under syllable keys, sorted by key, so a key prefix
// is a contiguous range: the main lexicon and the user's own words.
class KeyIndex {
public:
	struct Range {
		uint32_t begin;
		uint32_t end;
		bool Empty() const { return begin >= end; }
	};

	virtual ~KeyIndex() {}
	virtual uint32_t Size() const = 0;
	virtual int KeyLength(uint32_t index) const = 0;
	virtual SyllableId KeyAt(uint32_t index, int position) const = 0;

	Range All() const { return Range{0, Size()}; }
	// The entries in range (which share their first depth syllables) whose
	// next syllable is id.
	Range Narrow(Range range, int depth, SyllableId id) const;
	// The entries in range whose key is exactly depth syllables long; they
	// sort first.
	Range Exact(Range range, int depth) const;
};


class Lexicon : public KeyIndex {
public:
	Lexicon() {}
	~Lexicon();
	Lexicon(const Lexicon&) = delete;
	Lexicon& operator=(const Lexicon&) = delete;

	bool Load(const std::string& path, std::string* error = nullptr);
	bool Loaded() const { return fEntries != nullptr; }

	const Syllables& GetSyllables() const { return fSyllables; }

	uint32_t Size() const override { return fCount; }
	int KeyLength(uint32_t index) const override
		{ return fEntries[index].keyLength; }
	SyllableId KeyAt(uint32_t index, int position) const override
		{ return fKeys[fEntries[index].keyOffset + position]; }

	std::string_view Word(uint32_t index) const;
	float Cost(uint32_t index) const { return fEntries[index].cost; }

private:
	struct Entry {
		uint32_t keyOffset;
		uint32_t wordOffset;
		float cost;
		uint8_t keyLength;
		uint8_t wordLength;
		uint16_t reserved;
	};

	void _Unload();

	void* fMapping = nullptr;
	size_t fMappingSize = 0;
	Syllables fSyllables;
	const Entry* fEntries = nullptr;
	const SyllableId* fKeys = nullptr;
	const char* fWords = nullptr;
	uint32_t fCount = 0;
};

}	// namespace qingpin
