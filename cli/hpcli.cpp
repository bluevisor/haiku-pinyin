// hpcli: drive the Haiku Pinyin engine from a terminal, one line of keys at a
// time. Named keys go in angle brackets: <space> <ret> <bs> <esc> <left>
// <right> <up> <down> <pgup> <pgdn> <tab> <shift> (toggles 中/英).
//
//   hpcli [--data DIR] [--layout full|pyjj|xiaohe|ziranma|mspy|abc]
//         [--script s|t|tw|hk] [--page N] [--user FILE]
//         [--fuzzy MASK] [--vowels aa|oa|a]
//         [--lines 1]     each line typed afresh: keys, then its candidates,
//                         tab-separated; "<text before>\t<keys>" sets the
//                         context first
//
//   hpcli export FILE [--user FILE] [--data DIR]
//   hpcli import FILE [--user FILE] [--data DIR]
//         the learned words out to a text file, or words and phrases in
//         (ours, Sogou's, Rime's, the Mac's text replacements). --user
//         defaults to this system's user.dict, phrases.txt beside it; a
//         running input method picks up the change by itself.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include <sys/stat.h>

#include "../engine/Engine.h"

using namespace haikupinyin;


static bool
Exists(const std::string& path)
{
	struct stat info;
	return stat(path.c_str(), &info) == 0;
}


// Where the input method on this system keeps user.dict.
static std::string
DefaultUserDictionary()
{
	std::string home = getenv("HOME") != nullptr ? getenv("HOME") : ".";
#if defined(__APPLE__)
	std::string folder = home + "/Library/Application Support/HaikuPinyin";
#elif defined(__HAIKU__)
	std::string folder = home + "/config/settings/HaikuPinyin";
#else
	const char* config = getenv("XDG_CONFIG_HOME");
	std::string folder = (config != nullptr && config[0] != '\0'
		? std::string(config) : home + "/.config") + "/haiku-pinyin";
#endif
	mkdir(folder.c_str(), 0755);
	return folder + "/user.dict";
}


// build/data, else where this system's input method has its data.
static std::string
DefaultData()
{
	std::string home = getenv("HOME") != nullptr ? getenv("HOME") : ".";
	const std::string places[] = {
		"build/data",
#if defined(__APPLE__)
		home + "/Library/Input Methods/HaikuPinyin.app/Contents/Resources/data",
#elif defined(__HAIKU__)
		home + "/config/non-packaged/data/haiku-pinyin",
		home + "/config/data/haiku-pinyin",
		"/boot/system/non-packaged/data/haiku-pinyin",
		"/boot/system/data/haiku-pinyin",
#else
		home + "/.local/share/haiku-pinyin",
		"/usr/local/share/haiku-pinyin",
		"/usr/share/haiku-pinyin",
#endif
	};
	for (const std::string& place : places) {
		if (Exists(place + "/pinyin.dict"))
			return place;
	}
	return "build/data";
}


static int
ImportOrExport(int argc, char** argv)
{
	std::string command = argv[1], file = argv[2], data, user;
	for (int i = 3; i + 1 < argc; i += 2) {
		std::string flag = argv[i];
		if (flag == "--data")
			data = argv[i + 1];
		else if (flag == "--user")
			user = argv[i + 1];
		else {
			fprintf(stderr, "unknown option %s\n", flag.c_str());
			return 1;
		}
	}
	Engine engine;
	std::string error;
	if (!engine.Load(data.empty() ? DefaultData() : data, &error)) {
		fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	engine.SetUserDictionary(user.empty() ? DefaultUserDictionary() : user);

	if (command == "export") {
		int count = 0;
		if (!engine.ExportWords(file, &count)) {
			fprintf(stderr, "can't write %s\n", file.c_str());
			return 1;
		}
		printf("%d words to %s\n", count, file.c_str());
		return 0;
	}
	Engine::ImportResult result = engine.ImportWords(file);
	if (!result.error.empty()) {
		fprintf(stderr, "%s\n", result.error.c_str());
		return 1;
	}
	if (result.format.empty()) {
		fprintf(stderr, "nothing to import in %s\n", file.c_str());
		return 1;
	}
	printf("%s: %d words, %d phrases (%s), %d skipped\n",
		result.format.c_str(), result.words, result.phrases,
		engine.PhrasesFile().c_str(), result.skipped);
	return 0;
}


static void
Show(Engine& engine)
{
	std::string commit = engine.TakeCommit();
	if (!commit.empty())
		printf("commit: %s\n", commit.c_str());
	if (!engine.Composing())
		return;
	printf("  %s\n ", engine.Preedit().c_str());
	std::vector<std::string> page = engine.PageCandidates();
	std::string keys = engine.PickKeys();
	for (size_t i = 0; i < page.size(); i++) {
		bool highlighted = int(i) == engine.Highlighted();
		printf(" %s%c.%s%s", highlighted ? "[" : "",
			i < keys.size() ? keys[i] : ' ', page[i].c_str(),
			highlighted ? "]" : "");
	}
	printf("%s%s\n", engine.HasPreviousPage() ? "  <" : "",
		engine.HasNextPage() ? "  >" : "");
}


static void
Feed(Engine& engine, const std::string& line)
{
	struct Named { const char* name; Key key; };
	static const Named kNames[] = {
		{"space", Key::kSpace}, {"ret", Key::kReturn},
		{"bs", Key::kBackspace}, {"esc", Key::kEscape},
		{"left", Key::kLeft}, {"right", Key::kRight}, {"up", Key::kUp},
		{"down", Key::kDown}, {"pgup", Key::kPageUp},
		{"pgdn", Key::kPageDown}, {"tab", Key::kTab},
	};
	for (size_t i = 0; i < line.size(); i++) {
		if (line[i] == '<') {
			size_t end = line.find('>', i);
			if (end != std::string::npos) {
				std::string name = line.substr(i + 1, end - i - 1);
				bool known = false;
				if (name == "shift") {
					engine.SetChinese(!engine.Chinese());
					known = true;
				}
				for (const Named& named : kNames) {
					if (name == named.name) {
						if (!engine.ProcessKey(named.key, 0, 0))
							printf("(passed <%s>)\n", name.c_str());
						known = true;
					}
				}
				if (known) {
					i = end;
					continue;
				}
			}
		}
		char32_t character = static_cast<unsigned char>(line[i]);
		Key key = character == ' ' ? Key::kSpace : Key::kCharacter;
		if (!engine.ProcessKey(key, character, 0)) {
			std::string commit = engine.TakeCommit();
			printf("%s(passed '%c')\n",
				commit.empty() ? "" : ("commit: " + commit + "\n").c_str(),
				char(character));
		}
	}
	Show(engine);
}


int
main(int argc, char** argv)
{
	if (argc >= 3 && (strcmp(argv[1], "export") == 0
			|| strcmp(argv[1], "import") == 0)) {
		return ImportOrExport(argc, argv);
	}

	std::string data = "build/data", user;
	bool lines = false;
	Options options;
	for (int i = 1; i + 1 < argc; i += 2) {
		std::string flag = argv[i], value = argv[i + 1];
		if (flag == "--data")
			data = value;
		else if (flag == "--layout")
			options.layout = value;
		else if (flag == "--page")
			options.pageSize = atoi(value.c_str());
		else if (flag == "--user")
			user = value;
		else if (flag == "--lines")
			lines = value != "0";
		else if (flag == "--fuzzy")
			options.fuzzy = unsigned(atoi(value.c_str()));
		else if (flag == "--vowels") {
			options.vowels = value == "a" ? haikupinyin::Vowels::kSingle
				: value == "oa" ? haikupinyin::Vowels::kOLed
				: haikupinyin::Vowels::kDoubled;
		} else if (flag == "--script") {
			options.script = value == "t" ? Script::kTraditional
				: value == "tw" ? Script::kTaiwan
				: value == "hk" ? Script::kHongKong : Script::kSimplified;
		} else {
			fprintf(stderr, "unknown option %s\n", flag.c_str());
			return 1;
		}
	}

	Engine engine;
	std::string error;
	if (!engine.Load(data, &error)) {
		fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	engine.SetOptions(options);
	if (!user.empty())
		engine.SetUserDictionary(user);

	std::string line;
	while (std::getline(std::cin, line)) {
		if (!lines) {
			Feed(engine, line);
			continue;
		}
		// Each line typed afresh: "<keys>\t<candidate>\t<candidate>…".
		// "<text before>\t<keys>" gives it the text before as context.
		engine.Reset();
		engine.ClearContext();
		size_t tab = line.find('\t');
		if (tab != std::string::npos) {
			engine.SetContextText(line.substr(0, tab));
			line = line.substr(tab + 1);
		}
		for (char key : line) {
			engine.ProcessKey(haikupinyin::Key::kCharacter,
				static_cast<unsigned char>(key), 0);
		}
		printf("%s", line.c_str());
		for (const std::string& candidate : engine.PageCandidates())
			printf("\t%s", candidate.c_str());
		printf("\n");
		engine.Reset();
	}
	engine.SaveUserDictionary();
	return 0;
}
