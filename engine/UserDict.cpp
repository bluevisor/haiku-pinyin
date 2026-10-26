#include "UserDict.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace haikupinyin {

static const float kLowestCost = 2.0f;


static bool
WordLess(const UserDict::Word& word, const std::vector<SyllableId>& key,
	const std::string& text)
{
	if (word.key != key)
		return word.key < key;
	return word.text < text;
}


float
UserDict::Cost(uint32_t index) const
{
	const Word& word = fWords[index];
	// Picking a word once lifts it well above its neighbors; each later pick
	// lifts it a little less.
	float boost = 3.0f + 1.5f * std::log(float(word.count));
	return std::max(kLowestCost, word.cost - boost);
}


std::vector<UserDict::Word>::iterator
UserDict::_Find(const std::vector<SyllableId>& key, const std::string& text)
{
	return std::lower_bound(fWords.begin(), fWords.end(), key,
		[&](const Word& word, const std::vector<SyllableId>& target) {
			return WordLess(word, target, text);
		});
}


void
UserDict::Learn(const std::vector<SyllableId>& key, const std::string& text,
	float cost)
{
	if (key.empty() || text.empty())
		return;
	auto found = _Find(key, text);
	if (found != fWords.end() && found->key == key && found->text == text) {
		found->count++;
		found->cost = std::min(found->cost, cost);
		found->lastUsed = ++fClock;
	} else
		fWords.insert(found, Word{key, text, cost, 1, ++fClock});
	fDirty = true;
}


bool
UserDict::Forget(const std::vector<SyllableId>& key, const std::string& text)
{
	auto found = _Find(key, text);
	if (found == fWords.end() || found->key != key || found->text != text)
		return false;
	fWords.erase(found);
	fDirty = true;
	return true;
}


bool
UserDict::Load(const std::string& path, const Syllables& syllables)
{
	std::ifstream file(path);
	if (!file)
		return false;
	fWords.clear();
	std::string line;
	while (std::getline(file, line)) {
		// count \t last used \t cost \t text \t pin yin
		std::istringstream fields(line);
		std::string count, used, cost, text, spelling;
		if (!std::getline(fields, count, '\t')
			|| !std::getline(fields, used, '\t')
			|| !std::getline(fields, cost, '\t')
			|| !std::getline(fields, text, '\t')
			|| !std::getline(fields, spelling)) {
			continue;
		}
		Word word{{}, text, strtof(cost.c_str(), nullptr),
			uint32_t(strtoul(count.c_str(), nullptr, 10)),
			strtoull(used.c_str(), nullptr, 10)};
		std::istringstream names(spelling);
		std::string name;
		bool valid = word.count > 0;
		while (valid && names >> name) {
			int id = syllables.Find(name);
			valid = id >= 0;
			word.key.push_back(SyllableId(id));
		}
		if (!valid || word.key.empty())
			continue;
		fClock = std::max(fClock, word.lastUsed);
		fWords.push_back(std::move(word));
	}
	std::sort(fWords.begin(), fWords.end(), [](const Word& a, const Word& b) {
		return WordLess(a, b.key, b.text);
	});
	fWords.erase(std::unique(fWords.begin(), fWords.end(),
		[](const Word& a, const Word& b) {
			return a.key == b.key && a.text == b.text;
		}), fWords.end());
	fDirty = false;
	return true;
}


bool
UserDict::Save(const std::string& path, const Syllables& syllables)
{
	std::string temp = path + ".new";
	{
		std::ofstream file(temp, std::ios::trunc);
		if (!file)
			return false;
		for (const Word& word : fWords) {
			file << word.count << '\t' << word.lastUsed << '\t' << word.cost
				<< '\t' << word.text << '\t';
			for (size_t i = 0; i < word.key.size(); i++)
				file << (i > 0 ? " " : "") << syllables.Name(word.key[i]);
			file << '\n';
		}
		if (!file.flush())
			return false;
	}
	if (rename(temp.c_str(), path.c_str()) != 0)
		return false;
	fDirty = false;
	return true;
}

}	// namespace haikupinyin
