#include "Spelling.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

namespace haikupinyin {

// Costs added for guessing, in the same -log(p) units as word costs.
static const float kUnfinishedPenalty = 1.5f;
	// the last syllable is still being typed: "zho"
static const float kAbbreviationPenalty = 3.0f;
	// an initial standing for a whole syllable: "zgr" for 中国人

static const char* const kInitials[] = {
	"b", "p", "m", "f", "d", "t", "n", "l", "g", "k", "h", "j", "q", "x",
	"zh", "ch", "sh", "r", "z", "c", "s", "y", "w"
};


static bool
IsInitial(std::string_view keys)
{
	for (const char* initial : kInitials) {
		if (keys == initial)
			return true;
	}
	return false;
}


static bool
IsVowel(char c)
{
	return strchr("aeiouv", c) != nullptr;
}


void
SyllableGraph::Reset(std::string_view typed)
{
	keys = std::string(typed);
	length = int(typed.size());
	edges.clear();
	outgoing.assign(length + 1, std::vector<int>());
}


void
SyllableGraph::Add(SyllableEdge edge)
{
	outgoing[edge.from].push_back(int(edges.size()));
	edges.push_back(std::move(edge));
}


// #pragma mark - Fuzzy


static const float kFuzzyPenalty = 1.0f;


// The syllable's initial and the rest: "zhuang" -> "zh", "uang".
static void
SplitSyllable(const std::string& name, std::string& initial,
	std::string& final)
{
	size_t length = 0;
	if (name.size() >= 2 && name[1] == 'h' && strchr("zcs", name[0]) != nullptr)
		length = 2;
	else if (!name.empty() && strchr("bpmfdtnlgkhjqxrzcsyw", name[0]) != nullptr)
		length = 1;
	initial = name.substr(0, length);
	final = name.substr(length);
}


static std::vector<std::string>
InitialVariants(const std::string& initial, unsigned rules)
{
	std::vector<std::string> variants{initial};
	auto pair = [&](const char* a, const char* b) {
		if (initial == a)
			variants.push_back(b);
		else if (initial == b)
			variants.push_back(a);
	};
	if (rules & kFuzzyRetroflex) {
		pair("z", "zh");
		pair("c", "ch");
		pair("s", "sh");
	}
	if (rules & kFuzzyNL)
		pair("n", "l");
	if (rules & kFuzzyFH)
		pair("f", "h");
	if (rules & kFuzzyRL)
		pair("r", "l");
	return variants;
}


static std::vector<std::string>
FinalVariants(const std::string& final, unsigned rules)
{
	std::vector<std::string> variants{final};
	if ((rules & kFuzzyNasal) == 0)
		return variants;
	for (const char* nasal : {"an", "en", "in"}) {
		size_t size = final.size();
		if (size >= 2 && final.compare(size - 2, 2, nasal) == 0)
			variants.push_back(final + "g");
		else if (size >= 3 && final.compare(size - 3, 2, nasal) == 0
			&& final[size - 1] == 'g')
			variants.push_back(final.substr(0, size - 1));
	}
	return variants;
}


void
AddFuzzy(SyllableGraph& graph, const Syllables& syllables, unsigned rules)
{
	if (rules == 0)
		return;
	size_t count = graph.edges.size();
	for (size_t index = 0; index < count; index++) {
		// Copied: Add() may move the vector.
		SyllableEdge edge = graph.edges[index];
		if (!edge.complete || edge.ids.empty())
			continue;
		std::vector<SyllableId> variants;
		for (SyllableId id : edge.ids) {
			std::string initial, final;
			SplitSyllable(syllables.Name(id), initial, final);
			for (const std::string& i : InitialVariants(initial, rules)) {
				for (const std::string& f : FinalVariants(final, rules)) {
					int variant = syllables.Find(i + f);
					if (variant < 0 || std::find(edge.ids.begin(),
							edge.ids.end(), variant) != edge.ids.end()
						|| std::find(variants.begin(), variants.end(),
							variant) != variants.end()) {
						continue;
					}
					variants.push_back(SyllableId(variant));
				}
			}
		}
		if (variants.empty())
			continue;
		std::sort(variants.begin(), variants.end());
		graph.Add(SyllableEdge{edge.from, edge.to, variants, true,
			edge.penalty + kFuzzyPenalty, edge.display});
	}
}


// #pragma mark - FullPinyin


void
FullPinyin::Parse(std::string_view keys, const Syllables& syllables,
	SyllableGraph& graph)
{
	int length = int(keys.size());
	graph.Reset(keys);

	for (int from = 0; from < length; from++) {
		if (keys[from] == '\'') {
			graph.Add(SyllableEdge{from, from + 1, {}, true, 0, "'"});
			continue;
		}
		int segmentEnd = from;
		while (segmentEnd < length && keys[segmentEnd] != '\'')
			segmentEnd++;

		int longest = std::min(segmentEnd - from, syllables.MaxLength());
		for (int size = 1; size <= longest; size++) {
			std::string_view spelling = keys.substr(from, size);
			const std::vector<SyllableId>& starting
				= syllables.WithPrefix(spelling);
			if (starting.empty())
				break;

			int to = from + size;
			bool atEnd = to == segmentEnd;
			int id = syllables.Find(spelling);
			if (id >= 0) {
				graph.Add(SyllableEdge{from, to, {SyllableId(id)}, true, 0,
					std::string(spelling)});
				if (atEnd && starting.size() > 1) {
					// "xian" could still become "xiang".
					graph.Add(SyllableEdge{from, to, starting, false,
						kUnfinishedPenalty, std::string(spelling)});
				}
				continue;
			}

			if (atEnd) {
				graph.Add(SyllableEdge{from, to, starting, false,
					kUnfinishedPenalty, std::string(spelling)});
			} else if (IsInitial(spelling) && !IsVowel(keys[to])
				&& !(size == 1 && keys[to] == 'h'
					&& strchr("zcs", spelling[0]) != nullptr)) {
				graph.Add(SyllableEdge{from, to, starting, false,
					kAbbreviationPenalty, std::string(spelling)});
			}
		}
	}
}


// #pragma mark - Shuangpin


bool
Shuangpin::Load(const std::string& path, const Syllables& syllables,
	std::string* error)
{
	std::ifstream file(path);
	if (!file) {
		if (error != nullptr)
			*error = path + ": " + strerror(errno);
		return false;
	}
	fLayouts.clear();
	std::string line;
	while (std::getline(file, line)) {
		std::istringstream fields(line);
		std::string layout, code, name;
		if (!std::getline(fields, layout, '\t')
			|| !std::getline(fields, code, '\t')
			|| !std::getline(fields, name) || code.size() != 2) {
			continue;
		}
		int id = syllables.Find(name);
		if (id < 0)
			continue;
		Layout& target = fLayouts[layout];
		target.codes[code].push_back(SyllableId(id));
		std::vector<SyllableId>& first = target.firstKeys[code[0]];
		if (std::find(first.begin(), first.end(), id) == first.end())
			first.push_back(SyllableId(id));
		if (code.find(';') != std::string::npos)
			target.usesSemicolon = true;
	}
	for (auto& [name, layout] : fLayouts) {
		for (auto& [key, ids] : layout.firstKeys)
			std::sort(ids.begin(), ids.end());
	}
	return true;
}


bool
Shuangpin::HasLayout(const std::string& layout) const
{
	return fLayouts.count(layout) != 0;
}


bool
Shuangpin::IsKey(const std::string& layout, char key) const
{
	if (key >= 'a' && key <= 'z')
		return true;
	auto found = fLayouts.find(layout);
	return key == ';' && found != fLayouts.end() && found->second.usesSemicolon;
}


// What an unfinished syllable shows: the start all its candidates share
// ("zh" for v in most layouts), or the key itself.
static std::string
CommonStart(const std::vector<SyllableId>& ids, const Syllables& syllables,
	char key)
{
	if (ids.empty())
		return std::string(1, key);
	std::string common = syllables.Name(ids[0]);
	for (SyllableId id : ids) {
		const std::string& name = syllables.Name(id);
		size_t same = 0;
		while (same < common.size() && same < name.size()
			&& common[same] == name[same]) {
			same++;
		}
		common.resize(same);
	}
	return common.empty() ? std::string(1, key) : common;
}


void
Shuangpin::Parse(const std::string& layoutName, std::string_view keys,
	const Syllables& syllables, SyllableGraph& graph) const
{
	int length = int(keys.size());
	graph.Reset(keys);
	auto found = fLayouts.find(layoutName);
	if (found == fLayouts.end())
		return;
	const Layout& layout = found->second;

	int from = 0;
	while (from < length) {
		if (keys[from] == '\'') {
			graph.Add(SyllableEdge{from, from + 1, {}, true, 0, "'"});
			from++;
			continue;
		}
		if (from + 1 < length && keys[from + 1] != '\'') {
			auto code = layout.codes.find(std::string(keys.substr(from, 2)));
			if (code != layout.codes.end()) {
				std::string display = syllables.Name(code->second[0]);
				for (size_t i = 1; i < code->second.size(); i++)
					display += "/" + syllables.Name(code->second[i]);
				graph.Add(SyllableEdge{from, from + 2, code->second, true, 0,
					display});
			}
			from += 2;
			continue;
		}
		auto first = layout.firstKeys.find(keys[from]);
		if (first != layout.firstKeys.end()) {
			graph.Add(SyllableEdge{from, from + 1, first->second, false,
				kUnfinishedPenalty,
				CommonStart(first->second, syllables, keys[from])});
		}
		from++;
	}
}

}	// namespace haikupinyin
