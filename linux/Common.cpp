// 海酷拼音 Haiku Pinyin for Linux: settings, data, the user dictionary and
// the custom phrases, for both front ends.

#include "Common.h"

#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>

#ifndef HP_DATADIR
#define HP_DATADIR "/usr/share/haiku-pinyin"
#endif


static std::string
Home()
{
	const char* home = getenv("HOME");
	if (home != nullptr && home[0] != '\0')
		return home;
	struct passwd* entry = getpwuid(getuid());
	return entry != nullptr ? entry->pw_dir : "/";
}


// As GLib's g_get_user_config_dir() and friends, which IBus went by.
static std::string
XdgDirectory(const char* variable, const char* fallback)
{
	const char* value = getenv(variable);
	if (value != nullptr && value[0] != '\0')
		return value;
	return Home() + "/" + fallback;
}


static void
MakeDirectories(const std::string& path)
{
	for (size_t slash = path.find('/', 1); ; slash = path.find('/', slash + 1)) {
		mkdir(path.substr(0, slash).c_str(), 0755);
		if (slash == std::string::npos)
			break;
	}
}


std::string
ConfigDirectory()
{
	std::string path = XdgDirectory("XDG_CONFIG_HOME", ".config")
		+ "/haiku-pinyin";
	MakeDirectories(path);
	return path;
}


std::string
UserDictionaryPath()
{
	return ConfigDirectory() + "/user.dict";
}


std::string
PhrasesPath()
{
	return ConfigDirectory() + "/phrases.txt";
}


void
ReadSettings(haikupinyin::Options* options, bool* chinese)
{
	std::ifstream file(ConfigDirectory() + "/settings");
	std::string line;
	while (std::getline(file, line)) {
		size_t equals = line.find('=');
		if (equals == std::string::npos)
			continue;
		std::string name = line.substr(0, equals);
		std::string value = line.substr(equals + 1);
		bool on = value == "1" || value == "true";
		if (name == "layout")
			options->layout = value;
		else if (name == "script") {
			options->script = atoi(value.c_str()) == 0
				? haikupinyin::Script::kSimplified
				: haikupinyin::Script::kTraditional;
		} else if (name == "page_size") {
			int size = atoi(value.c_str());
			if (size >= 3 && size <= 10)
				options->pageSize = size;
		} else if (name == "fullwidth")
			options->fullwidthPunctuation = on;
		else if (name == "emoji")
			options->emoji = on;
		else if (name == "english")
			options->english = on;
		else if (name == "cloud")
			options->cloud = on;
		else if (name == "fuzzy")
			options->fuzzy = unsigned(atoi(value.c_str()));
		else if (name == "vowels") {
			int vowels = atoi(value.c_str());
			if (vowels >= 0 && vowels <= 2)
				options->vowels = haikupinyin::Vowels(vowels);
		} else if (name == "chinese")
			*chinese = on;
	}
}


bool
WriteSettings(const haikupinyin::Options& options, bool chinese)
{
	std::string path = ConfigDirectory() + "/settings";
	std::string staging = path + ".new";
	{
		std::ofstream file(staging);
		file << "layout=" << options.layout << "\n"
			<< "script=" << (options.script == haikupinyin::Script::kSimplified
				? 0 : 1) << "\n"
			<< "page_size=" << options.pageSize << "\n"
			<< "fullwidth=" << options.fullwidthPunctuation << "\n"
			<< "emoji=" << options.emoji << "\n"
			<< "english=" << options.english << "\n"
			<< "cloud=" << options.cloud << "\n"
			<< "fuzzy=" << options.fuzzy << "\n"
			<< "vowels=" << int(options.vowels) << "\n"
			<< "chinese=" << chinese << "\n";
		if (!file)
			return false;
	}
	return rename(staging.c_str(), path.c_str()) == 0;
}


bool
SameSettings(const haikupinyin::Options& a, const haikupinyin::Options& b)
{
	return a.layout == b.layout && a.script == b.script
		&& a.pageSize == b.pageSize
		&& a.fullwidthPunctuation == b.fullwidthPunctuation
		&& a.emoji == b.emoji && a.english == b.english && a.cloud == b.cloud
		&& a.fuzzy == b.fuzzy && a.vowels == b.vowels;
}


std::vector<std::string>
DataDirectories()
{
	std::vector<std::string> places;
	if (const char* data = getenv("HAIKU_PINYIN_DATA"))
		places.push_back(data);
	places.push_back(XdgDirectory("XDG_DATA_HOME", ".local/share")
		+ "/haiku-pinyin");
	const char* system = getenv("XDG_DATA_DIRS");
	std::string dirs = system != nullptr && system[0] != '\0' ? system
		: "/usr/local/share:/usr/share";
	size_t start = 0;
	while (start <= dirs.size()) {
		size_t colon = dirs.find(':', start);
		if (colon == std::string::npos)
			colon = dirs.size();
		if (colon > start)
			places.push_back(dirs.substr(start, colon - start) + "/haiku-pinyin");
		start = colon + 1;
	}
	places.push_back(HP_DATADIR);
	return places;
}


bool
LoadData(haikupinyin::Engine* engine, std::string* error)
{
	*error = "no haiku-pinyin data directory";
	for (const std::string& place : DataDirectories()) {
		if (engine->Load(place, error)) {
			engine->SetUserDictionary(UserDictionaryPath());
			engine->SetPhrasesFile(PhrasesPath());
			return true;
		}
	}
	return false;
}
