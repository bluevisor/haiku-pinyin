#include "Decoder.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace haikupinyin {

static const int kLongestWord = 12;			// syllables
static const float kWordPenalty = 2.0f;		// per word in a sentence:
	// fewer, longer words read better
static const float kRawCost = 30.0f;		// a key no word explains
static const size_t kKeepFromStart = 600;	// candidates per span
static const size_t kKeepElsewhere = 4;		// only the sentence needs these


Decoder::Decoder(const Lexicon& lexicon, const UserDict& user)
	:
	fLexicon(lexicon),
	fUser(user)
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
				word.cost = fUser.Cost(i) + penalty;
				word.source = WordEdge::kUser;
			} else {
				word.text = std::string(fLexicon.Word(i));
				word.cost = fLexicon.Cost(i) + penalty;
				word.source = WordEdge::kLexicon;
			}
			for (int k = 0; k < depth; k++)
				word.key.push_back(keys.KeyAt(i, k));
			word.syllables = path;
			words.push_back(std::move(word));
		}
	}
}


void
Decoder::_FindBest(const SyllableGraph& graph)
{
	const float kNone = std::numeric_limits<float>::infinity();
	std::vector<float> best(fLength + 1, kNone);
	std::vector<const WordEdge*> back(fLength + 1, nullptr);
	best[0] = 0;

	// Every key can always be passed over as itself, so a path exists.
	fRaw.reserve(fLength);
	for (int at = 0; at < fLength; at++) {
		char key = graph.keys[at];
		bool separator = key == '\'';
		fRaw.push_back(WordEdge{at, at + 1,
			separator ? std::string() : std::string(1, key),
			separator ? 0 : kRawCost, WordEdge::kRaw, {}, {}});
	}

	for (int from = 0; from < fLength; from++) {
		if (best[from] == kNone)
			continue;
		auto relax = [&](const WordEdge& word, float extra) {
			float cost = best[from] + word.cost + extra;
			if (cost < best[word.to]) {
				best[word.to] = cost;
				back[word.to] = &word;
			}
		};
		relax(fRaw[from], 0);
		for (int to = from + 1; to <= fLength; to++) {
			if (!fEdges[from][to].empty())
				relax(fEdges[from][to].front(), kWordPenalty);
		}
	}

	for (int at = fLength; at > 0 && back[at] != nullptr;
			at = back[at]->from) {
		fBest.push_back(back[at]);
	}
	std::reverse(fBest.begin(), fBest.end());
}


std::vector<Candidate>
Decoder::Candidates(size_t limit) const
{
	std::vector<Candidate> candidates;
	std::unordered_set<std::string> seen;

	if (!fBest.empty()) {
		Candidate sentence{"", fLength, 0, {}, false};
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
		}
		sentence.sentence = words > 1;
		if (raw)
			sentence.key.clear();	// keys and text no longer line up
		if (!sentence.text.empty()) {
			seen.insert(sentence.text);
			candidates.push_back(std::move(sentence));
		}
	}

	if (fLength == 0)
		return candidates;
	for (int to = fLength; to > 0; to--) {
		for (const WordEdge& word : fEdges[0][to]) {
			if (candidates.size() >= limit)
				return candidates;
			if (seen.insert(word.text).second) {
				candidates.push_back(Candidate{word.text, to, word.cost,
					word.key, false});
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
