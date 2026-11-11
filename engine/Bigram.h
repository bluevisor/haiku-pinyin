// Haiku Pinyin: word pairs, for sentences that read naturally. From
// libpinyin's open-gram model.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace haikupinyin {

class Bigram {
public:
	Bigram() {}
	~Bigram();
	Bigram(const Bigram&) = delete;
	Bigram& operator=(const Bigram&) = delete;

	// bigram.bin; it may be missing.
	void Load(const std::string& directory);
	bool Loaded() const { return fPairs != nullptr; }

	static uint32_t Hash(std::string_view word);

	// ln P(second | first), or false when the pair isn't known.
	bool LogProbability(uint32_t first, uint32_t second, float& logp) const;
	// Whether any pair starts with first: a word worth keeping as context.
	bool HasFollowers(uint32_t first) const;
	// A word the pairs know, in their counts.
	struct Word {
		float logp;		// ln P(word)
		float endLift;	// ln(P(end | word) / P(end)): how much likelier
						// than usual a sentence ends after it
	};
	// False when the pairs don't know the word (or the file predates the
	// table).
	bool Find(uint32_t word, Word& found) const;
	bool HasWords() const { return fWords != nullptr; }

private:
	struct Pair {
		uint32_t first;
		uint32_t second;
		float logp;
	};
	struct Mapping {
		void* address = nullptr;
		size_t size = 0;
	};

	static const char* _Map(const std::string& path, const char* magic,
		Mapping& mapping);

	struct WordRecord {
		uint32_t word;
		Word values;
	};

	Mapping fPairFile;
	const Pair* fPairs = nullptr;
	uint32_t fPairCount = 0;
	const WordRecord* fWords = nullptr;
	uint32_t fWordCount = 0;
};

}	// namespace haikupinyin
