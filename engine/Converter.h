// Qingpin: simplified -> traditional text, longest match first, from OpenCC's
// tables.
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace qingpin {

class Converter {
public:
	// "<from>\t<to>" lines.
	bool Load(const std::string& path);
	bool Loaded() const { return !fTable.empty(); }
	std::string Convert(std::string_view text) const;

private:
	std::unordered_map<std::string, std::string> fTable;
	size_t fLongest = 0;	// in characters
};

}	// namespace qingpin
