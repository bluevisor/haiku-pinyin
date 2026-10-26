// Haiku Pinyin: word pairs, for sentences that read naturally and for
// predicting the next word (联想). From libpinyin's open-gram model.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace haikupinyin {

class Bigram {
public:
	Bigram() {}
	~Bigram();
	Bigram(const Bigram&) = delete;
	Bigram& operator=(const Bigram&) = delete;

	// bigram.bin and predict.bin; either may be missing.
	void Load(const std::string& directory);
	bool Loaded() const { return fPairs != nullptr; }

	static uint32_t Hash(std::string_view word);

	// ln P(second | first), or false when the pair isn't known.
	bool LogProbability(uint32_t first, uint32_t second, float& logp) const;
	// The likeliest words after first, best first.
	std::vector<std::string> Predict(uint32_t first, size_t limit) const;

private:
	struct Pair {
		uint32_t first;
		uint32_t second;
		float logp;
	};
	struct Prediction {
		uint32_t first;
		uint32_t offset;
		uint32_t length;
		float logp;
	};
	struct Mapping {
		void* address = nullptr;
		size_t size = 0;
	};

	static const char* _Map(const std::string& path, const char* magic,
		Mapping& mapping);

	Mapping fPairFile;
	Mapping fPredictionFile;
	const Pair* fPairs = nullptr;
	uint32_t fPairCount = 0;
	const Prediction* fPredictions = nullptr;
	uint32_t fPredictionCount = 0;
	const char* fWords = nullptr;
	uint32_t fWordBytes = 0;
};

}	// namespace haikupinyin
