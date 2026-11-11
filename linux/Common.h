// 海酷拼音 Haiku Pinyin for Linux: what the IBus engine and the Fcitx5
// addon share, without either's headers. Both keep their settings and the
// words they learn in the same place, so switching frameworks keeps them.
#pragma once

#include <string>
#include <vector>

#include "../engine/Engine.h"

// ~/.config/haiku-pinyin ($XDG_CONFIG_HOME), made if it's missing.
std::string ConfigDirectory();
// The words it learns, in ConfigDirectory().
std::string UserDictionaryPath();
// The custom phrases, beside them.
std::string PhrasesPath();

// ConfigDirectory()/settings: options keeps its defaults for anything the
// file doesn't name.
void ReadSettings(haikupinyin::Options* options, bool* chinese);
bool WriteSettings(const haikupinyin::Options& options, bool chinese);

// Where to look for the data, in order: $HAIKU_PINYIN_DATA, then
// haiku-pinyin/ in the XDG data directories, then HP_DATADIR.
std::vector<std::string> DataDirectories();
// Loads the dictionaries from the first of those that has them, and points
// the user dictionary at UserDictionaryPath() and the custom phrases at
// PhrasesPath(), which the engine reads again as a composition starts when
// it changed.
bool LoadData(haikupinyin::Engine* engine, std::string* error);
