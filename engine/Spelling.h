// Qingpin: turning typed keys into a graph of possible syllables, for full
// pinyin and for the shuangpin layouts.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "Lexicon.h"

namespace qingpin {

// Keys [from, to) spell any one of ids. A complete edge is one whole
// syllable; an incomplete one is a prefix ("zh", "zho") standing for every
// syllable it starts. An edge with no ids is a separator (').
struct SyllableEdge {
	int from;
	int to;
	std::vector<SyllableId> ids;
	bool complete;
	float penalty;
	std::string display;
};

struct SyllableGraph {
	std::string keys;
	int length = 0;
	std::vector<SyllableEdge> edges;
	// Edge indices by starting position.
	std::vector<std::vector<int>> outgoing;

	void Reset(std::string_view keys);
	void Add(SyllableEdge edge);
};


class FullPinyin {
public:
	static void Parse(std::string_view keys, const Syllables& syllables,
		SyllableGraph& graph);
};


class Shuangpin {
public:
	// shuangpin.txt: "<layout>\t<code>\t<syllable>" lines.
	bool Load(const std::string& path, const Syllables& syllables,
		std::string* error = nullptr);
	bool HasLayout(const std::string& layout) const;
	// The keys a layout uses beyond a-z (";" for some).
	bool IsKey(const std::string& layout, char key) const;
	void Parse(const std::string& layout, std::string_view keys,
		const Syllables& syllables, SyllableGraph& graph) const;

private:
	struct Layout {
		std::map<std::string, std::vector<SyllableId>> codes;
		std::map<char, std::vector<SyllableId>> firstKeys;
		bool usesSemicolon = false;
	};

	std::map<std::string, Layout> fLayouts;
};

}	// namespace qingpin
