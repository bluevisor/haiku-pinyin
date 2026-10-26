#include "Decoder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace haikupinyin {

static const int kLongestWord = 12;			// syllables
static const float kWordPenalty = 2.0f;		// per word in a sentence:
	// fewer, longer words read better
static const float kRawCost = 30.0f;		// a key no word explains
static const size_t kKeepFromStart = 600;	// candidates per span
static const size_t kKeepElsewhere = 6;		// the sentence's alternatives
static const size_t kBeam = 8;				// sentence prefixes kept per key
static const size_t kWordsTried = 5;		// per span, by the beam
static const float kBigramWeight = 0.4f;	// λ in λ·P(w|prev) + (1-λ)·P(w)
static const float kBestBigramBonus = -6.0f;


Decoder::Decoder(const Lexicon& lexicon, const UserDict& user,
	const Bigram& bigram)
	:
	fLexicon(lexicon),
	fUser(user),
	fBigram(bigram)
{
}


void
Decoder::Decode(const SyllableGraph& graph)
{
	fLength = graph.length;
	fEdges.assign(fLength + 1,
		std::vector<std::vector<WordEdge>>(fLength + 1));
	fBest.clear();
	fRaw.clear();

	for (int start = 0; start < fLength; start++)
		_Expand(graph, start);

	for (int from = 0; from < fLength; from++) {
		size_t keep = from == 0 ? kKeepFromStart : kKeepElsewhere;
		for (std::vector<WordEdge>& words : fEdges[from]) {
			std::stable_sort(words.begin(), words.end(),
				[](const WordEdge& a, const WordEdge& b) {
					return a.cost < b.cost;
				});
			// The same text can come from the lexicon and the user's words,
			// or from several readings: keep the cheapest.
			std::unordered_set<std::string> seen;
			std::vector<WordEdge> unique;
			for (WordEdge& word : words) {
				if (unique.size() >= keep)
					break;
				if (seen.insert(word.text).second)
					unique.push_back(std::move(word));
			}
			words.swap(unique);
		}
	}
	_FindBest(graph);
}


void
Decoder::_Expand(const SyllableGraph& graph, int start)
{
	std::vector<Branch> branches{{fLexicon.All(), false}};
	if (fUser.Size() > 0)
		branches.push_back(Branch{fUser.All(), true});
	std::vector<int> path;
	_Walk(graph, start, start, 0, branches, 0, path);
}


void
Decoder::_Walk(const SyllableGraph& graph, int start, int position, int depth,
	const std::vector<Branch>& branches, float penalty, std::vector<int>& path)
{
	if (depth >= kLongestWord)
		return;

	for (int index : graph.outgoing[position]) {
		const SyllableEdge& edge = graph.edges[index];
		if (edge.ids.empty()) {
			// A separator: words may run across it, but not start with it.
			if (depth > 0) {
				path.push_back(index);
				_Walk(graph, start, edge.to, depth, branches, penalty, path);
				path.pop_back();
			}
			continue;
		}

		std::vector<Branch> next;
		bool longer = false;
		for (const Branch& branch : branches) {
			const KeyIndex& keys = branch.user
				? static_cast<const KeyIndex&>(fUser) : fLexicon;
			for (SyllableId id : edge.ids) {
				KeyIndex::Range range = keys.Narrow(branch.range, depth, id);
				if (range.Empty())
					continue;
				next.push_back(Branch{range, branch.user});
				if (keys.Exact(range, depth + 1).end < range.end)
					longer = true;
			}
		}
		if (next.empty())
			continue;

		path.push_back(index);
		_Emit(start, edge.to, next, depth + 1, penalty + edge.penalty, path);
		if (longer) {
			_Walk(graph, start, edge.to, depth + 1, next,
				penalty + edge.penalty, path);
		}
		path.pop_back();
	}
}


void
Decoder::_Emit(int from, int to, const std::vector<Branch>& branches,
	int depth, float penalty, const std::vector<int>& path)
{
	std::vector<WordEdge>& words = fEdges[from][to];
	for (const Branch& branch : branches) {
		const KeyIndex& keys = branch.user
			? static_cast<const KeyIndex&>(fUser) : fLexicon;
		KeyIndex::Range exact = keys.Exact(branch.range, depth);
		for (uint32_t i = exact.begin; i < exact.end; i++) {
			WordEdge word;
			word.from = from;
			word.to = to;
			if (branch.user) {
				word.text = fUser.At(i).text;
				word.base = fUser.Cost(i);
				word.source = WordEdge::kUser;
			} else {
				word.text = std::string(fLexicon.Word(i));
				word.base = fLexicon.Cost(i);
				word.source = WordEdge::kLexicon;
			}
			word.cost = word.base + penalty;
			word.hash = Bigram::Hash(word.text);
			for (int k = 0; k < depth; k++)
				word.key.push_back(keys.KeyAt(i, k));
			word.syllables = path;
			words.push_back(std::move(word));
		}
	}
}


// What following previous costs on top of the word's own cost: with a
// known pair, -ln(λ·P(w|prev)/P(w) + 1-λ), a bonus; without, -ln(1-λ).
float
Decoder::_Transition(uint32_t previous, const WordEdge& word) const
{
	if (previous == 0 || word.source == WordEdge::kRaw || !fBigram.Loaded())
		return 0;
	float logp;
	if (!fBigram.LogProbability(previous, word.hash, logp))
		return -std::log(1 - kBigramWeight);
	float ratio = std::exp(std::min(logp + word.base, 30.0f));
	return std::max(kBestBigramBonus,
		-std::log(kBigramWeight * ratio + 1 - kBigramWeight));
}


// A beam search for the likeliest sentence: at each key position, the best
// few sentence prefixes, one per last word, since the next word's cost
// depends on it.
void
Decoder::_FindBest(const SyllableGraph& graph)
{
	struct Prefix {
		float cost;
		const WordEdge* word;
		int from;		// where the previous prefix ends
		int previous;	// and which one it is there
		uint32_t last;	// the last word's hash (0 after raw keys)
	};
	std::vector<std::vector<Prefix>> prefixes(fLength + 1);
	prefixes[0].push_back(Prefix{0, nullptr, -1, -1, fContext});

	// Every key can always be passed over as itself, so a path exists.
	fRaw.reserve(fLength);
	for (int at = 0; at < fLength; at++) {
		char key = graph.keys[at];
		bool separator = key == '\'';
		fRaw.push_back(WordEdge{at, at + 1,
			separator ? std::string() : std::string(1, key),
			separator ? 0 : kRawCost, separator ? 0 : kRawCost, 0,
			WordEdge::kRaw, {}, {}});
	}

	auto offer = [&](int to, Prefix prefix) {
		std::vector<Prefix>& here = prefixes[to];
		for (Prefix& other : here) {
			if (other.last == prefix.last) {
				if (prefix.cost < other.cost)
					other = prefix;
				return;
			}
		}
		if (here.size() < kBeam) {
			here.push_back(prefix);
			return;
		}
		auto worst = std::max_element(here.begin(), here.end(),
			[](const Prefix& a, const Prefix& b) { return a.cost < b.cost; });
		if (prefix.cost < worst->cost)
			*worst = prefix;
	};

	for (int from = 0; from < fLength; from++) {
		for (int index = 0; index < int(prefixes[from].size()); index++) {
			const Prefix prefix = prefixes[from][index];
			const WordEdge& raw = fRaw[from];
			offer(from + 1, Prefix{prefix.cost + raw.cost, &raw, from, index,
				raw.text.empty() ? prefix.last : 0});
			for (int to = from + 1; to <= fLength; to++) {
				const std::vector<WordEdge>& words = fEdges[from][to];
				for (size_t i = 0; i < words.size() && i < kWordsTried; i++) {
					const WordEdge& word = words[i];
					float cost = prefix.cost + word.cost + kWordPenalty
						+ _Transition(prefix.last, word);
					offer(to, Prefix{cost, &word, from, index, word.hash});
				}
			}
		}
	}

	const std::vector<Prefix>& ends = prefixes[fLength];
	if (ends.empty())
		return;
	int at = fLength;
	int index = int(std::min_element(ends.begin(), ends.end(),
		[](const Prefix& a, const Prefix& b) { return a.cost < b.cost; })
		- ends.begin());
	while (at > 0) {
		const Prefix& prefix = prefixes[at][index];
		fBest.push_back(prefix.word);
		at = prefix.from;
		index = prefix.previous;
	}
	std::reverse(fBest.begin(), fBest.end());
}


std::vector<Candidate>
Decoder::Candidates(const SyllableGraph& graph, size_t limit) const
{
	std::vector<Candidate> candidates;
	std::unordered_set<std::string> seen;

	if (!fBest.empty()) {
		Candidate sentence{"", fLength, 0, {}, false, ""};
		int words = 0;
		bool raw = false;
		for (const WordEdge* word : fBest) {
			sentence.text += word->text;
			sentence.cost += word->cost;
			sentence.key.insert(sentence.key.end(), word->key.begin(),
				word->key.end());
			if (word->source != WordEdge::kRaw)
				words++;
			else if (!word->text.empty())
				raw = true;
			for (int index : word->syllables) {
				if (!graph.edges[index].complete)
					sentence.guessed = true;
			}
		}
		sentence.sentence = words > 1;
		sentence.raw = raw;
		if (raw)
			sentence.key.clear();	// keys and text no longer line up
		for (auto word = fBest.rbegin(); word != fBest.rend(); word++) {
			if ((*word)->source != WordEdge::kRaw) {
				sentence.tail = (*word)->text;
				break;
			}
		}
		if (!sentence.text.empty()) {
			seen.insert(sentence.text);
			candidates.push_back(std::move(sentence));
		}
	}

	if (fLength == 0)
		return candidates;
	for (int to = fLength; to > 0; to--) {
		// After a word you've just typed, what usually follows it first.
		std::vector<std::pair<float, const WordEdge*>> words;
		for (const WordEdge& word : fEdges[0][to])
			words.emplace_back(word.cost + _Transition(fContext, word), &word);
		std::stable_sort(words.begin(), words.end(),
			[](const auto& a, const auto& b) { return a.first < b.first; });
		for (const auto& [cost, word] : words) {
			if (candidates.size() >= limit)
				return candidates;
			if (seen.insert(word->text).second) {
				candidates.push_back(Candidate{word->text, to, word->cost,
					word->key, false, word->text});
			}
		}
	}
	return candidates;
}


std::string
Decoder::Spelled(const SyllableGraph& graph) const
{
	// Syllables are split by ', keys no word explains run together.
	std::string spelled;
	bool lastRaw = false;
	auto add = [&](const std::string& piece, bool raw) {
		if (!spelled.empty() && spelled.back() != '\'' && !(raw && lastRaw))
			spelled += '\'';
		spelled += piece;
		lastRaw = raw;
	};
	for (const WordEdge* word : fBest) {
		if (word->source == WordEdge::kRaw) {
			if (word->text.empty()) {
				spelled += '\'';
				lastRaw = false;
			} else
				add(word->text, true);
			continue;
		}
		for (int index : word->syllables) {
			const SyllableEdge& edge = graph.edges[index];
			if (!edge.ids.empty())
				add(edge.display, false);
		}
	}
	return spelled;
}

}	// namespace haikupinyin
