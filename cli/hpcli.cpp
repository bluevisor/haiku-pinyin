// hpcli: drive the Haiku Pinyin engine from a terminal, one line of keys at a
// time. Named keys go in angle brackets: <space> <ret> <bs> <esc> <left>
// <right> <up> <down> <pgup> <pgdn> <tab> <shift> (toggles 中/英).
//
//   hpcli [--data DIR] [--layout full|pyjj|xiaohe|ziranma|mspy|abc]
//         [--script s|t|tw|hk] [--page N] [--user FILE]
//         [--fuzzy MASK] [--predict 0|1]

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include "../engine/Engine.h"

using namespace haikupinyin;


static void
Show(Engine& engine)
{
	std::string commit = engine.TakeCommit();
	if (!commit.empty())
		printf("commit: %s\n", commit.c_str());
	if (engine.Predicting()) {
		printf("  联想:");
		for (const std::string& word : engine.PageCandidates())
			printf(" %s", word.c_str());
		printf("\n");
		return;
	}
	if (!engine.Composing())
		return;
	printf("  %s\n ", engine.Preedit().c_str());
	std::vector<std::string> page = engine.PageCandidates();
	for (size_t i = 0; i < page.size(); i++) {
		bool highlighted = int(i) == engine.Highlighted();
		printf(" %s%zu.%s%s", highlighted ? "[" : "", i + 1, page[i].c_str(),
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
	std::string data = "build/data", user;
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
		else if (flag == "--fuzzy")
			options.fuzzy = unsigned(atoi(value.c_str()));
		else if (flag == "--predict")
			options.prediction = value != "0";
		else if (flag == "--script") {
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
	while (std::getline(std::cin, line))
		Feed(engine, line);
	engine.SaveUserDictionary();
	return 0;
}
