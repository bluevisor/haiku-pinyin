#include "UserDict.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace haikupinyin {

static const float kLowestCost = 2.0f;


// By key, then text: <0, 0 or >0.
static int
Compare(const std::vector<SyllableId>& key, const std::string& text,
	const std::vector<SyllableId>& otherKey, const std::string& otherText)
{
	if (key != otherKey)
		return key < otherKey ? -1 : 1;
	return text.compare(otherText);
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


uint64_t
UserDict::Time()
{
	uint64_t now = fNow != 0 ? fNow : uint64_t(time(nullptr));
	fClock = std::max(fClock + 1, now);
	return fClock;
}


std::vector<UserDict::Word>::iterator
UserDict::_Find(const std::vector<SyllableId>& key, const std::string& text)
{
	return std::lower_bound(fWords.begin(), fWords.end(), key,
		[&](const Word& word, const std::vector<SyllableId>& target) {
			return Compare(word.key, word.text, target, text) < 0;
		});
}


std::vector<UserDict::Forgotten>::iterator
UserDict::_FindForgotten(const std::vector<SyllableId>& key,
	const std::string& text)
{
	return std::lower_bound(fForgotten.begin(), fForgotten.end(), key,
		[&](const Forgotten& word, const std::vector<SyllableId>& target) {
			return Compare(word.key, word.text, target, text) < 0;
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
		found->lastUsed = Time();
	} else
		fWords.insert(found, Word{key, text, cost, 1, Time()});
	fDirty = true;
}


bool
UserDict::Forget(const std::vector<SyllableId>& key, const std::string& text)
{
	auto found = _Find(key, text);
	if (found == fWords.end() || found->key != key || found->text != text)
		return false;
	fWords.erase(found);
	auto forgotten = _FindForgotten(key, text);
	if (forgotten != fForgotten.end() && forgotten->key == key
		&& forgotten->text == text) {
		forgotten->time = Time();
	} else
		fForgotten.insert(forgotten, Forgotten{key, text, Time()});
	fDirty = true;
	return true;
}


bool
UserDict::Merge(const std::vector<SyllableId>& key, const std::string& text,
	float cost, uint32_t count)
{
	if (key.empty() || text.empty() || count == 0)
		return false;
	auto found = _Find(key, text);
	if (found != fWords.end() && found->key == key && found->text == text) {
		if (found->count >= count && found->cost <= cost)
			return false;
		found->count = std::max(found->count, count);
		found->cost = std::min(found->cost, cost);
	} else
		fWords.insert(found, Word{key, text, cost, count, Time()});
	fDirty = true;
	return true;
}


void
UserDict::Add(const std::vector<SyllableId>& key, const std::string& text,
	float cost, uint32_t count, uint64_t time)
{
	if (key.empty() || text.empty())
		return;
	fClock = std::max(fClock, time);
	auto forgotten = _FindForgotten(key, text);
	bool there = forgotten != fForgotten.end() && forgotten->key == key
		&& forgotten->text == text;
	auto found = _Find(key, text);
	bool known = found != fWords.end() && found->key == key
		&& found->text == text;
	if (count == 0) {
		if (there)
			forgotten->time = std::max(forgotten->time, time);
		else
			forgotten = fForgotten.insert(forgotten, Forgotten{key, text, time});
		if (known && found->lastUsed <= forgotten->time)
			fWords.erase(found);
		return;
	}
	if (there && forgotten->time >= time)
		return;
	if (known) {
		found->count = std::max(found->count, count);
		found->cost = std::min(found->cost, cost);
		found->lastUsed = std::max(found->lastUsed, time);
	} else
		fWords.insert(found, Word{key, text, cost, count, time});
}


int
UserDict::Join(const UserDict& other)
{
	int changes = 0;
	// Both lists are sorted: walk them side by side.
	std::vector<Forgotten> forgotten;
	forgotten.reserve(fForgotten.size() + other.fForgotten.size());
	size_t i = 0, j = 0;
	while (i < fForgotten.size() || j < other.fForgotten.size()) {
		int order = i == fForgotten.size() ? 1
			: j == other.fForgotten.size() ? -1
			: Compare(fForgotten[i].key, fForgotten[i].text,
				other.fForgotten[j].key, other.fForgotten[j].text);
		if (order < 0)
			forgotten.push_back(std::move(fForgotten[i++]));
		else if (order > 0) {
			forgotten.push_back(other.fForgotten[j++]);
			changes++;
		} else {
			Forgotten entry = std::move(fForgotten[i++]);
			if (other.fForgotten[j].time > entry.time) {
				entry.time = other.fForgotten[j].time;
				changes++;
			}
			j++;
			forgotten.push_back(std::move(entry));
		}
	}

	// Words come out in order, so the forgotten one for each, if any, is
	// at or after the last one looked at.
	size_t next = 0;
	auto isForgotten = [&](const Word& word) {
		while (next < forgotten.size()
			&& Compare(forgotten[next].key, forgotten[next].text, word.key,
				word.text) < 0) {
			next++;
		}
		return next < forgotten.size() && forgotten[next].key == word.key
			&& forgotten[next].text == word.text
			&& forgotten[next].time >= word.lastUsed;
	};
	std::vector<Word> words;
	words.reserve(fWords.size() + other.fWords.size());
	i = j = 0;
	while (i < fWords.size() || j < other.fWords.size()) {
		int order = i == fWords.size() ? 1 : j == other.fWords.size() ? -1
			: Compare(fWords[i].key, fWords[i].text, other.fWords[j].key,
				other.fWords[j].text);
		if (order < 0) {
			Word word = std::move(fWords[i++]);
			if (isForgotten(word))
				changes++;
			else
				words.push_back(std::move(word));
		} else if (order > 0) {
			const Word& word = other.fWords[j++];
			if (!isForgotten(word)) {
				words.push_back(word);
				changes++;
			}
		} else {
			Word word = std::move(fWords[i++]);
			const Word& theirs = other.fWords[j++];
			bool changed = theirs.count > word.count || theirs.cost < word.cost
				|| theirs.lastUsed > word.lastUsed;
			word.count = std::max(word.count, theirs.count);
			word.cost = std::min(word.cost, theirs.cost);
			word.lastUsed = std::max(word.lastUsed, theirs.lastUsed);
			if (isForgotten(word))
				changes++;
			else {
				changes += changed;
				words.push_back(std::move(word));
			}
		}
	}
	fWords = std::move(words);
	fForgotten = std::move(forgotten);
	fClock = std::max(fClock, other.fClock);
	if (changes > 0)
		fDirty = true;
	return changes;
}


bool
UserDict::Load(const std::string& path, const Syllables& syllables)
{
	std::ifstream file(path);
	if (!file)
		return false;
	fWords.clear();
	fForgotten.clear();
	std::string line;
	while (std::getline(file, line)) {
		// count \t last used \t cost \t text \t pin yin; count 0: forgotten
		std::istringstream fields(line);
		std::string count, used, cost, text, spelling;
		if (!std::getline(fields, count, '\t')
			|| !std::getline(fields, used, '\t')
			|| !std::getline(fields, cost, '\t')
			|| !std::getline(fields, text, '\t')
			|| !std::getline(fields, spelling)
			|| count.empty() || count[0] < '0' || count[0] > '9') {
			continue;
		}
		std::vector<SyllableId> key;
		std::istringstream names(spelling);
		std::string name;
		bool valid = true;
		while (valid && names >> name) {
			int id = syllables.Find(name);
			valid = id >= 0;
			key.push_back(SyllableId(id));
		}
		if (!valid)
			continue;
		// Saved in order, so each goes in at the end.
		Add(key, text, strtof(cost.c_str(), nullptr),
			uint32_t(strtoul(count.c_str(), nullptr, 10)),
			strtoull(used.c_str(), nullptr, 10));
	}
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
		auto spell = [&](const std::vector<SyllableId>& key) {
			for (size_t i = 0; i < key.size(); i++)
				file << (i > 0 ? " " : "") << syllables.Name(key[i]);
			file << '\n';
		};
		for (const Word& word : fWords) {
			file << word.count << '\t' << word.lastUsed << '\t' << word.cost
				<< '\t' << word.text << '\t';
			spell(word.key);
		}
		for (const Forgotten& word : fForgotten) {
			file << "0\t" << word.time << "\t0\t" << word.text << '\t';
			spell(word.key);
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
