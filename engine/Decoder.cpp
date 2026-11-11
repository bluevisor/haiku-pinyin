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
static const size_t kBeam = 12;				// sentence prefixes kept per key
static const size_t kPerLastWord = 2;		// of them ending in the same word
static const size_t kWordsTried = 5;		// per span, by the beam
static const float kBigramWeight = 0.4f;	// λ in λ·P(w|prev) + (1-λ)·P(w)
static const float kBestBigramBonus = -6.0f;
static const float kEndWeight = 0.5f;		// μ, the same for P(end|w)
static const size_t kOtherSentences = 2;	// offered after the best
static const float kOtherSentenceMargin = 4.0f;	// at most this much costlier
static const size_t kEndsKept = 100000;		// words whose ends are known


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
	fOthers.clear();
	fRaw.clear();

	for (int start = 0; start < fLength; start++)
		_Expand(graph, start);

	for (int from = 0; from < fLength; from++) {
		size_t keep = from == 0 ? kKeepFromStart : kKeepElsewhere;
		for (std::vector<WordEdge>& words : fEdges[from]) {
			for (WordEdge& word : words)
				_Ends(word);
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


// The lexicon has phrases the pairs don't (我想, 上海的, 考虑一下): for
// those, the longest start and end that are words to the pairs stand in.
// Kept by text, since every key typed meets the same words again.
void
Decoder::_Ends(WordEdge& word) const
{
	word.head = word.tail = word.hash;
	word.tailStart = 0;
	word.headKnown = false;
	word.headLogp = 0;
	word.tailEnd = 0;
	if (word.source == WordEdge::kRaw || !fBigram.HasWords())
		return;
	if (fEnds.size() > kEndsKept)
		fEnds.clear();
	auto [found, added] = fEnds.try_emplace(word.text);
	Ends& ends = found->second;
	if (added)
		ends = _FindEnds(word.text, word.hash);
	if (ends.head != 0)
		word.head = ends.head;
	if (ends.tail != 0)
		word.tail = ends.tail;
	word.tailStart = ends.tailStart;
	word.headKnown = ends.headKnown;
	word.headLogp = ends.headLogp;
	word.tailEnd = ends.tailEnd;
	word.cost += ends.penalty;
}


Decoder::Ends
Decoder::_FindEnds(const std::string& text, uint32_t hash) const
{
	Ends ends;
	Bigram::Word found;
	if (fBigram.Find(hash, found)) {
		ends.headKnown = true;
		ends.headLogp = found.logp;
		ends.tailEnd = found.endLift;
		return ends;
	}
	std::vector<size_t> starts;
	for (size_t i = 0; i < text.size(); i++) {
		if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80)
			starts.push_back(i);
	}
	size_t headEnd = 0;
	for (size_t n = starts.size() - 1; n > 0; n--) {
		uint32_t head = Bigram::Hash(std::string_view(text).substr(0,
			starts[n]));
		if (fBigram.Find(head, found)) {
			ends.head = head;
			ends.headKnown = true;
			ends.headLogp = found.logp;
			headEnd = starts[n];
			break;
		}
	}
	for (size_t n = 1; n < starts.size(); n++) {
		uint32_t tail = Bigram::Hash(std::string_view(text).substr(starts[n]));
		if (fBigram.Find(tail, found)) {
			ends.tail = tail;
			ends.tailStart = starts[n];
			ends.tailEnd = found.endLift;
			break;
		}
	}
	// Two words the pairs never saw together (再学习: 再 then 学习) make a
	// phrase that counts as the two, as far as the word penalty goes.
	float logp;
	if (headEnd != 0 && headEnd == ends.tailStart
		&& !fBigram.LogProbability(ends.head, ends.tail, logp))
		ends.penalty = kWordPenalty;
	return ends;
}


// What following previous costs on top of the word's own cost: with a
// known pair, -ln(λ·P(w|prev)/P(w) + 1-λ), a bonus; without, -ln(1-λ).
// P(w|prev)/P(w) is how much likelier the pair makes the word, both from
// the pairs' own counts: the lexicon's costs are on another scale.
float
Decoder::_Transition(uint32_t previous, const WordEdge& word) const
{
	if (previous == 0 || word.source == WordEdge::kRaw || !fBigram.Loaded())
		return 0;
	float logp;
	if (!fBigram.LogProbability(previous, word.head, logp))
		return -std::log(1 - kBigramWeight);
	// Older data has no words alone: the lexicon's cost stands in.
	float lift = logp + (word.headKnown ? -word.headLogp : word.base);
	float ratio = std::exp(std::min(lift, 30.0f));
	return std::max(kBestBigramBonus,
		-std::log(kBigramWeight * ratio + 1 - kBigramWeight));
}


// What ending the input after word costs, the same way: -ln(μ·P(end|w)/P(end)
// + 1-μ). 吧 and 吗 end sentences, 把 and 码 seldom do.
float
Decoder::_End(const WordEdge& word) const
{
	if (word.source == WordEdge::kRaw)
		return 0;
	return -std::log(kEndWeight * std::exp(word.tailEnd) + 1 - kEndWeight);
}


// A beam search for the likeliest sentence: at each key position, the best
// few sentence prefixes, at most two per last word, since the next word's
// cost depends on it (and the second best of each makes the other sentences
// on offer differ where it counts, 在 or 再, not only at the end).
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
		size_t same = 0;
		Prefix* worstSame = nullptr;
		for (Prefix& other : here) {
			if (other.last != prefix.last)
				continue;
			same++;
			if (worstSame == nullptr || other.cost > worstSame->cost)
				worstSame = &other;
		}
		if (same >= kPerLastWord) {
			if (prefix.cost < worstSame->cost)
				*worstSame = prefix;
			return;
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
					offer(to, Prefix{cost, &word, from, index, word.tail});
				}
			}
		}
	}

	// Ending a sentence of several words weighs in at the end.
	std::vector<std::pair<float, int>> ends;
	for (int index = 0; index < int(prefixes[fLength].size()); index++) {
		const Prefix& prefix = prefixes[fLength][index];
		ends.emplace_back(prefix.cost
			+ (prefix.from > 0 ? _End(*prefix.word) : 0), index);
	}
	std::sort(ends.begin(), ends.end());
	auto path = [&](int index) {
		std::vector<const WordEdge*> words;
		for (int at = fLength; at > 0;) {
			const Prefix& prefix = prefixes[at][index];
			words.push_back(prefix.word);
			at = prefix.from;
			index = prefix.previous;
		}
		std::reverse(words.begin(), words.end());
		return words;
	};
	if (ends.empty())
		return;
	fBest = path(ends[0].second);

	// The next best sentences of several words. At most one of them may
	// differ from those before it only in its last word (在哪里 for 在那里);
	// the others differ ahead of it (再 for 在 in 我需要再确认一下), rather
	// than offering 电影爸 after 电影吧.
	struct Read {
		std::string text;
		size_t lastStart;	// where its last word starts (bytes)
	};
	auto read = [](const std::vector<const WordEdge*>& words, Read& sentence) {
		int count = 0;
		sentence.text.clear();
		for (const WordEdge* word : words) {
			if (word->source == WordEdge::kRaw) {
				if (!word->text.empty())
					return false;
				continue;
			}
			sentence.lastStart = sentence.text.size();
			sentence.text += word->text;
			count++;
		}
		return count > 1;
	};
	std::vector<Read> offered(1);
	if (!read(fBest, offered[0]))
		return;
	bool lastOnly = false;
	for (size_t i = 1; i < ends.size() && fOthers.size() < kOtherSentences; i++) {
		if (ends[i].first > ends[0].first + kOtherSentenceMargin)
			break;
		std::vector<const WordEdge*> words = path(ends[i].second);
		Read sentence;
		if (!read(words, sentence))
			continue;
		bool ahead = true;
		for (const Read& other : offered) {
			size_t at = std::mismatch(sentence.text.begin(), sentence.text.end(),
				other.text.begin(), other.text.end()).first
				- sentence.text.begin();
			if (at >= std::min(sentence.lastStart, other.lastStart))
				ahead = false;
		}
		if (!ahead) {
			if (lastOnly)
				continue;
			lastOnly = true;
		}
		offered.push_back(sentence);
		fOthers.push_back(std::move(words));
	}
}


// A sentence along words, as a candidate for all the keys.
Candidate
Decoder::_Sentence(const SyllableGraph& graph,
	const std::vector<const WordEdge*>& path) const
{
	Candidate sentence{"", fLength, 0, {}, false, ""};
	int words = 0;
	bool raw = false;
	for (const WordEdge* word : path) {
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
	for (auto word = path.rbegin(); word != path.rend(); word++) {
		if ((*word)->source != WordEdge::kRaw) {
			sentence.tail = (*word)->text.substr((*word)->tailStart);
			break;
		}
	}
	return sentence;
}


std::vector<Candidate>
Decoder::Candidates(const SyllableGraph& graph, size_t limit) const
{
	std::vector<Candidate> candidates;
	std::unordered_set<std::string> seen;

	if (!fBest.empty()) {
		Candidate sentence = _Sentence(graph, fBest);
		if (!sentence.text.empty()) {
			seen.insert(sentence.text);
			candidates.push_back(std::move(sentence));
			// When the best reads as several words, the next best that do
			// too: the right one is often among them when it isn't first.
			for (size_t i = 0; i < fOthers.size() && candidates.size() < limit;
					i++) {
				Candidate other = _Sentence(graph, fOthers[i]);
				if (seen.insert(other.text).second)
					candidates.push_back(std::move(other));
			}
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
					word->key, false, word->text.substr(word->tailStart)});
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
