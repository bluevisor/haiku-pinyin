#include "Bigram.h"

#include <algorithm>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace haikupinyin {


Bigram::~Bigram()
{
	if (fPairFile.address != nullptr)
		munmap(fPairFile.address, fPairFile.size);
}


uint32_t
Bigram::Hash(std::string_view word)
{
	uint32_t hash = 0x811c9dc5;
	for (unsigned char c : word)
		hash = (hash ^ c) * 0x01000193;
	return hash;
}


// Maps a file that starts with the 8-byte magic; returns what follows it.
const char*
Bigram::_Map(const std::string& path, const char* magic, Mapping& mapping)
{
	int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0)
		return nullptr;
	struct stat st;
	if (fstat(fd, &st) != 0 || st.st_size < 16) {
		close(fd);
		return nullptr;
	}
	void* address = mmap(nullptr, size_t(st.st_size), PROT_READ, MAP_PRIVATE,
		fd, 0);
	close(fd);
	if (address == MAP_FAILED)
		return nullptr;
	if (memcmp(address, magic, 8) != 0) {
		munmap(address, size_t(st.st_size));
		return nullptr;
	}
	mapping.address = address;
	mapping.size = size_t(st.st_size);
	return static_cast<const char*>(address) + 8;
}


void
Bigram::Load(const std::string& directory)
{
	if (const char* data = _Map(directory + "/bigram.bin", "HPBIGR01",
			fPairFile)) {
		uint32_t count;
		memcpy(&count, data, 4);
		if (16 + size_t(count) * sizeof(Pair) <= fPairFile.size + 4) {
			fPairs = reinterpret_cast<const Pair*>(data + 4);
			fPairCount = count;
		}
	}
}


bool
Bigram::LogProbability(uint32_t first, uint32_t second, float& logp) const
{
	if (fPairs == nullptr)
		return false;
	const Pair* end = fPairs + fPairCount;
	const Pair* found = std::lower_bound(fPairs, end, Pair{first, second, 0},
		[](const Pair& a, const Pair& b) {
			return a.first != b.first ? a.first < b.first : a.second < b.second;
		});
	if (found == end || found->first != first || found->second != second)
		return false;
	logp = found->logp;
	return true;
}


bool
Bigram::HasFollowers(uint32_t first) const
{
	if (fPairs == nullptr)
		return false;
	const Pair* end = fPairs + fPairCount;
	const Pair* found = std::lower_bound(fPairs, end, first,
		[](const Pair& pair, uint32_t key) { return pair.first < key; });
	return found != end && found->first == first;
}

}	// namespace haikupinyin
