// Engine tests: engine_test <data directory>

#include <cstdio>
#include <string>
#include <unistd.h>

#include "../engine/Engine.h"

using namespace haikupinyin;

static int sFailures = 0;
static std::string sData;


static void
Type(Engine& engine, const std::string& keys)
{
	for (char key : keys) {
		engine.ProcessKey(key == ' ' ? Key::kSpace : Key::kCharacter,
			static_cast<unsigned char>(key), 0);
	}
}


static std::string
First(Engine& engine)
{
	std::vector<std::string> page = engine.PageCandidates();
	return page.empty() ? std::string() : page[0];
}


static void
Expect(const char* what, const std::string& got, const std::string& want)
{
	if (got == want)
		return;
	printf("FAIL %s: got \"%s\", want \"%s\"\n", what, got.c_str(),
		want.c_str());
	sFailures++;
}


static void
ExpectFirst(const char* layout, Script script, const std::string& keys,
	const std::string& want)
{
	Engine engine;
	engine.Load(sData);
	Options options;
	options.layout = layout;
	options.script = script;
	engine.SetOptions(options);
	Type(engine, keys);
	Expect((std::string(layout) + " " + keys).c_str(), First(engine), want);
}


int
main(int argc, char** argv)
{
	sData = argc > 1 ? argv[1] : "build/data";

	// Full pinyin: sentences, abbreviations, unfinished syllables, ü as v.
	ExpectFirst("full", Script::kSimplified, "nihao", "你好");
	ExpectFirst("full", Script::kSimplified, "woshizhongguoren", "我是中国人");
	ExpectFirst("full", Script::kSimplified, "zgr", "中国人");
	ExpectFirst("full", Script::kSimplified, "bjdx", "北京大学");
	ExpectFirst("full", Script::kSimplified, "zhongg", "中国");
	ExpectFirst("full", Script::kSimplified, "xi'an", "西安");
	ExpectFirst("full", Script::kSimplified, "xian", "先");
	ExpectFirst("full", Script::kSimplified, "lvse", "绿色");

	// Shuangpin layouts.
	ExpectFirst("pyjj", Script::kSimplified, "vygo", "中国");
	ExpectFirst("pyjj", Script::kSimplified, "nihd", "你好");
	ExpectFirst("pyjj", Script::kSimplified, "iurufa", "输入法");
		// pyjj: sh=i, so 输 is "iu"; "uu" is 出
	ExpectFirst("xiaohe", Script::kSimplified, "vsgo", "中国");
	ExpectFirst("xiaohe", Script::kSimplified, "uurufa", "输入法");
	ExpectFirst("ziranma", Script::kSimplified, "vsgo", "中国");
	ExpectFirst("mspy", Script::kSimplified, "bngr", "宾馆");
	ExpectFirst("abc", Script::kSimplified, "asgo", "中国");

	// Traditional output.
	ExpectFirst("full", Script::kTraditional, "zhongguoren", "中國人");
	ExpectFirst("full", Script::kTaiwan, "ruanjian", "軟體");
	ExpectFirst("full", Script::kHongKong, "toufa", "頭髮");

	// Punctuation, and digits after which it stays ASCII.
	{
		Engine engine;
		engine.Load(sData);
		Type(engine, "nihao,");
		Expect("comma commits", engine.TakeCommit(), "你好，");
		Type(engine, "\"");
		Expect("open quote", engine.TakeCommit(), "“");
		engine.ProcessKey(Key::kCharacter, '3', 0);
		Type(engine, ".");
		Expect("decimal point", engine.TakeCommit(), "");
	}

	// Picking in pieces commits once, and backspace reopens the last pick.
	{
		Engine engine;
		engine.Load(sData);
		Type(engine, "zhongguoren");
		engine.ProcessKey(Key::kPageDown, 0, 0);
		engine.ProcessKey(Key::kPageUp, 0, 0);
		// Find 中国 and pick it, leaving "ren".
		std::vector<std::string> page = engine.PageCandidates();
		int at = -1;
		for (size_t i = 0; i < page.size(); i++) {
			if (page[i] == "中国")
				at = int(i);
		}
		engine.SelectOnPage(at);
		Expect("partial preedit", engine.Preedit(), "中国ren");
		engine.ProcessKey(Key::kBackspace, 0, 0);
		engine.ProcessKey(Key::kBackspace, 0, 0);
		engine.ProcessKey(Key::kBackspace, 0, 0);
		Expect("pick reopened", engine.Preedit(), "zhong'guo");
		engine.ProcessKey(Key::kEscape, 0, 0);
		Expect("escape cancels", engine.Preedit() + engine.TakeCommit(), "");
	}

	// Learning: a word picked once moves to the front.
	{
		char path[] = "/tmp/haiku-pinyin-test-XXXXXX";
		int fd = mkstemp(path);
		close(fd);
		{
			Engine engine;
			engine.Load(sData);
			engine.SetUserDictionary(path);
			Type(engine, "shi");
			std::vector<std::string> page = engine.PageCandidates();
			std::string pick = page.size() > 4 ? page[4] : "";
			engine.SelectOnPage(4);
			Expect("learned commit", engine.TakeCommit(), pick);
			engine.SaveUserDictionary();
			Type(engine, "shi");
			Expect("learned first", First(engine), pick);
		}
		Engine reloaded;
		reloaded.Load(sData);
		reloaded.SetUserDictionary(path);
		Type(reloaded, "shi");
		std::string first = First(reloaded);
		reloaded.ProcessKey(Key::kEscape, 0, 0);
		Engine fresh;
		fresh.Load(sData);
		Type(fresh, "shi");
		Expect("learning persisted", first == First(fresh) ? "same" : "moved",
			"moved");
		unlink(path);
	}

	// English mode passes letters through; switching mid-word keeps them.
	{
		Engine engine;
		engine.Load(sData);
		Type(engine, "hel");
		engine.SetChinese(false);
		Expect("switch keeps letters", engine.TakeCommit(), "hel");
		Expect("english passes",
			engine.ProcessKey(Key::kCharacter, 'l', 0) ? "used" : "passed",
			"passed");
	}

	printf(sFailures == 0 ? "all passed\n" : "%d failed\n", sFailures);
	return sFailures == 0 ? 0 : 1;
}
