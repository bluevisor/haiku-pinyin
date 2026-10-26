// Haiku Pinyin: finds the words a syllable graph can spell, the likeliest
// sentence through them, and the candidates to offer.
#pragma once

#include <string>
#include <vector>

#include "Bigram.h"
#include "Lexicon.h"
#include "Spelling.h"
#include "UserDict.h"

namespace haikupinyin {

struct WordEdge {
	enum Source { kLexicon, kUser, kRaw };

	int from;
	int to;
	std::string text;
	float cost;			// base plus the guessing penalties
	float base;			// the word's own -ln p
	uint32_t hash;		// Bigram::Hash(text)
	Source source;
	std::vector<SyllableId> key;
	std::vector<int> syllables;	// the graph edges it was spelled over
};

struct Candidate {
	std::string text;
	int consumed;		// keys used up from the start
	float cost;
	std::vector<SyllableId> key;
	bool sentence;		// built from several words
	std::string tail;	// the last word, for what follows (联想, bigrams)
	bool raw = false;	// keys no word explains are in it
	bool guessed = false;	// some syllables were abbreviated or unfinished
	bool cloud = false;	// from the cloud
	bool english = false;
};


class Decoder {
public:
	Decoder(const Lexicon& lexicon, const UserDict& user,
		const Bigram& bigram);

	// The word before the input (0: none), for the first word's bigram.
	void SetContext(uint32_t previous) { fContext = previous; }

	void Decode(const SyllableGraph& graph);

	// The best way to spell the whole input, as word edges.
	const std::vector<const WordEdge*>& Best() const { return fBest; }
	// Sentence first, then words from the start, longer spans first.
	std::vector<Candidate> Candidates(const SyllableGraph& graph,
		size_t limit) const;
	// The input split into syllables along the best sentence: "zhong'guo".
	std::string Spelled(const SyllableGraph& graph) const;

private:
	struct Branch {
		KeyIndex::Range range;
		bool user;
	};

	void _Expand(const SyllableGraph& graph, int start);
	void _Walk(const SyllableGraph& graph, int start, int position, int depth,
		const std::vector<Branch>& branches, float penalty,
		std::vector<int>& path);
	void _Emit(int from, int to, const std::vector<Branch>& branches,
		int depth, float penalty, const std::vector<int>& path);
	void _FindBest(const SyllableGraph& graph);
	float _Transition(uint32_t previous, const WordEdge& word) const;

	const Lexicon& fLexicon;
	const UserDict& fUser;
	const Bigram& fBigram;
	uint32_t fContext = 0;
	// fEdges[from][to]: words spanning those keys, cheapest first.
	std::vector<std::vector<std::vector<WordEdge>>> fEdges;
	std::vector<const WordEdge*> fBest;
	std::vector<WordEdge> fRaw;
	int fLength = 0;
};

}	// namespace haikupinyin
