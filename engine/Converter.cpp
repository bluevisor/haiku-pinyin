#include "Converter.h"

#include <algorithm>
#include <fstream>
#include <vector>

namespace haikupinyin {


static size_t
CharacterCount(std::string_view text)
{
	size_t count = 0;
	for (unsigned char c : text)
		count += (c & 0xc0) != 0x80;
	return count;
}


bool
Converter::Load(const std::string& path)
{
	std::ifstream file(path);
	if (!file)
		return false;
	std::string line;
	while (std::getline(file, line)) {
		size_t tab = line.find('\t');
		if (tab == std::string::npos || tab == 0)
			continue;
		std::string from = line.substr(0, tab);
		fLongest = std::max(fLongest, CharacterCount(from));
		fTable.emplace(std::move(from), line.substr(tab + 1));
	}
	return !fTable.empty();
}


std::string
Converter::Convert(std::string_view text) const
{
	if (fTable.empty())
		return std::string(text);

	std::vector<size_t> starts;
	for (size_t i = 0; i < text.size(); i++) {
		if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80)
			starts.push_back(i);
	}
	starts.push_back(text.size());

	std::string result;
	size_t characters = starts.size() - 1;
	for (size_t at = 0; at < characters;) {
		size_t span = std::min(fLongest, characters - at);
		for (; span > 0; span--) {
			std::string piece(text.substr(starts[at],
				starts[at + span] - starts[at]));
			auto found = fTable.find(piece);
			if (found != fTable.end()) {
				result += found->second;
				break;
			}
		}
		if (span == 0) {
			result.append(text.substr(starts[at], starts[at + 1] - starts[at]));
			span = 1;
		}
		at += span;
	}
	return result;
}

}	// namespace haikupinyin
