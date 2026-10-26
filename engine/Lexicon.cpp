#include "Lexicon.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace haikupinyin {


void
Syllables::Add(std::string name)
{
	SyllableId id = SyllableId(fNames.size());
	fIds[name] = id;
	for (size_t length = 1; length <= name.size(); length++)
		fPrefixes[name.substr(0, length)].push_back(id);
	fMaxLength = std::max(fMaxLength, int(name.size()));
	fNames.push_back(std::move(name));
}


int
Syllables::Find(std::string_view name) const
{
	auto found = fIds.find(std::string(name));
	return found == fIds.end() ? -1 : found->second;
}


const std::vector<SyllableId>&
Syllables::WithPrefix(std::string_view prefix) const
{
	static const std::vector<SyllableId> kNone;
	auto found = fPrefixes.find(std::string(prefix));
	return found == fPrefixes.end() ? kNone : found->second;
}


// #pragma mark - KeyIndex


KeyIndex::Range
KeyIndex::Narrow(Range range, int depth, SyllableId id) const
{
	// Within the range, entries order by the syllable at depth, with keys
	// that end before it first: treat those as -1.
	auto at = [&](uint32_t index) {
		return KeyLength(index) > depth ? int(KeyAt(index, depth)) : -1;
	};
	uint32_t low = range.begin, high = range.end;
	while (low < high) {
		uint32_t middle = low + (high - low) / 2;
		if (at(middle) < id)
			low = middle + 1;
		else
			high = middle;
	}
	uint32_t begin = low;
	high = range.end;
	while (low < high) {
		uint32_t middle = low + (high - low) / 2;
		if (at(middle) <= id)
			low = middle + 1;
		else
			high = middle;
	}
	return Range{begin, low};
}


KeyIndex::Range
KeyIndex::Exact(Range range, int depth) const
{
	uint32_t low = range.begin, high = range.end;
	while (low < high) {
		uint32_t middle = low + (high - low) / 2;
		if (KeyLength(middle) <= depth)
			low = middle + 1;
		else
			high = middle;
	}
	return Range{range.begin, low};
}


// #pragma mark - Lexicon


Lexicon::~Lexicon()
{
	_Unload();
}


bool
Lexicon::Load(const std::string& path, std::string* error)
{
	_Unload();
	auto fail = [&](const char* why) {
		if (error != nullptr)
			*error = path + ": " + why;
		_Unload();
		return false;
	};

	int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return fail(strerror(errno));
	struct stat st;
	if (fstat(fd, &st) != 0 || st.st_size < 24) {
		close(fd);
		return fail("too short");
	}
	fMappingSize = size_t(st.st_size);
	fMapping = mmap(nullptr, fMappingSize, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (fMapping == MAP_FAILED) {
		fMapping = nullptr;
		return fail("mmap failed");
	}

	const char* data = static_cast<const char*>(fMapping);
	const char* end = data + fMappingSize;
	if (memcmp(data, "HPDICT01", 8) != 0)
		return fail("not a Haiku Pinyin dictionary");
	uint32_t header[4];
	memcpy(header, data + 8, sizeof(header));
	uint32_t syllables = header[0], keyUnits = header[2],
		wordBytes = header[3];
	fCount = header[1];

	const char* cursor = data + 24;
	for (uint32_t i = 0; i < syllables; i++) {
		if (cursor >= end)
			return fail("truncated syllables");
		uint8_t length = uint8_t(*cursor++);
		if (cursor + length > end)
			return fail("truncated syllables");
		fSyllables.Add(std::string(cursor, length));
		cursor += length;
	}
	cursor += (4 - (cursor - data) % 4) % 4;

	size_t need = size_t(fCount) * sizeof(Entry) + keyUnits * 2 + wordBytes;
	if (size_t(end - cursor) < need)
		return fail("truncated entries");
	fEntries = reinterpret_cast<const Entry*>(cursor);
	fKeys = reinterpret_cast<const SyllableId*>(cursor + fCount * sizeof(Entry));
	fWords = cursor + fCount * sizeof(Entry) + keyUnits * 2;
	return true;
}


std::string_view
Lexicon::Word(uint32_t index) const
{
	const Entry& entry = fEntries[index];
	return std::string_view(fWords + entry.wordOffset, entry.wordLength);
}


void
Lexicon::_Unload()
{
	if (fMapping != nullptr)
		munmap(fMapping, fMappingSize);
	fMapping = nullptr;
	fMappingSize = 0;
	fEntries = nullptr;
	fKeys = nullptr;
	fWords = nullptr;
	fCount = 0;
	fSyllables = Syllables();
}

}	// namespace haikupinyin
