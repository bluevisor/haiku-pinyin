// Engine tests: engine_test <data directory>

#include <cstdio>
#include <string>
#include <unistd.h>

#include "../engine/Engine.h"
#include "../engine/VMode.h"

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
	const std::string& want, Vowels vowels = Vowels::kDoubled)
{
	Engine engine;
	engine.Load(sData);
	Options options;
	options.layout = layout;
	options.script = script;
	options.vowels = vowels;
	engine.SetOptions(options);
	Type(engine, keys);
	Expect((std::string(layout) + " " + keys).c_str(), First(engine), want);
}


// How the keys were read: "sha'wan'yi'er'a'wei'shen'me".
static void
ExpectSpelled(const char* layout, Vowels vowels, const std::string& keys,
	const std::string& want)
{
	Engine engine;
	engine.Load(sData);
	Options options;
	options.layout = layout;
	options.vowels = vowels;
	engine.SetOptions(options);
	Type(engine, keys);
	static const char* const kNames[] = {"aa", "oa", "a"};
	Expect((std::string(layout) + " " + kNames[int(vowels)] + " " + keys
		+ " spelled").c_str(), engine.Spelled(), want);
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
	ExpectFirst("mspy", Script::kSimplified, "vsgo", "中国");
	// 微软 types ing with ;.
	ExpectSpelled("mspy", Vowels::kDoubled, "b;", "bing");
	// The last key alone stands for every syllable it starts; ' separates.
	ExpectFirst("pyjj", Script::kSimplified, "vyg", "中国");
	ExpectSpelled("pyjj", Vowels::kDoubled, "ni'hd", "ni'hao");

	// a, o and e, three ways, in every layout.
	for (const char* layout : {"pyjj", "xiaohe", "ziranma", "mspy", "abc"}) {
		bool abc = std::string(layout) == "abc";
		// Doubled; in 智能ABC aa and ee are zha and che, so a and e keep
		// their own oa and oe.
		ExpectSpelled(layout, Vowels::kDoubled, "aa", abc ? "zha" : "a");
		ExpectSpelled(layout, Vowels::kDoubled, "ee", abc ? "che" : "e");
		ExpectSpelled(layout, Vowels::kDoubled, "oo", "o");
		if (abc)
			ExpectSpelled(layout, Vowels::kDoubled, "oa", "a");
		ExpectSpelled(layout, Vowels::kOLed, "oa", "a");
		ExpectSpelled(layout, Vowels::kOLed, "oe", "e");
		ExpectSpelled(layout, Vowels::kOLed, "oo", "o");
		if (!abc) {
			// One key each: two a's are two syllables, and oa isn't a.
			ExpectSpelled(layout, Vowels::kSingle, "aa", "a'a");
			ExpectFirst(layout, Script::kSimplified, "oa", "啊", Vowels::kOLed);
			ExpectFirst(layout, Script::kSimplified, "aa", "啊", Vowels::kDoubled);
		}
	}
	// The pairs line up again after a single vowel, anywhere.
	ExpectSpelled("pyjj", Vowels::kSingle, "iawfyieqawwirme",
		"sha'wan'yi'er'a'wei'shen'me");
	ExpectFirst("pyjj", Script::kSimplified, "iawfyieqawwirme",
		"啥玩意儿啊为什么", Vowels::kSingle);
	ExpectSpelled("pyjj", Vowels::kSingle, "ewo", "e'wo");
	ExpectSpelled("pyjj", Vowels::kSingle, "awoaiwo", "a'wo'a'shei'o");
	// …and the other ways spell the same sentence with their own pairs.
	ExpectFirst("pyjj", Script::kSimplified, "iawfyieqaawwirme",
		"啥玩意儿啊为什么", Vowels::kDoubled);
	ExpectFirst("pyjj", Script::kSimplified, "iawfyieqoawwirme",
		"啥玩意儿啊为什么", Vowels::kOLed);
	// Other ways' codes are not a: oa when doubled, aa when o-led, left as
	// typed.
	ExpectSpelled("xiaohe", Vowels::kDoubled, "oaaa", "oa'a");
	ExpectSpelled("pyjj", Vowels::kOLed, "aaoa", "aa'a");
	ExpectFirst("xiaohe", Script::kSimplified, "vsgo", "中国");
	ExpectFirst("xiaohe", Script::kSimplified, "uurufa", "输入法");
	ExpectFirst("ziranma", Script::kSimplified, "vsgo", "中国");
	ExpectFirst("mspy", Script::kSimplified, "bngr", "宾馆");
	ExpectFirst("abc", Script::kSimplified, "asgo", "中国");

	// Traditional output.
	ExpectFirst("full", Script::kTraditional, "zhongguoren", "中國人");
	ExpectFirst("full", Script::kTaiwan, "ruanjian", "軟體");
	ExpectFirst("full", Script::kHongKong, "toufa", "頭髮");

	// Emoji follow their word, and switch off.
	{
		Engine engine;
		engine.Load(sData);
		Type(engine, "taiyang");
		std::vector<std::string> page = engine.PageCandidates();
		bool sun = false;
		for (const std::string& candidate : page)
			sun = sun || candidate == "🌞";
		Expect("太阳 offers 🌞", sun ? "yes" : "no", "yes");
		Options options;
		options.emoji = false;
		engine.SetOptions(options);
		Type(engine, "taiyang");
		page = engine.PageCandidates();
		Expect("emoji off", page.size() > 1 ? page[1] : "", "抬眼");
	}

	// 模糊音: z for zh, an for ang.
	{
		Engine engine;
		engine.Load(sData);
		Options options;
		options.fuzzy = kFuzzyRetroflex | kFuzzyNasal;
		engine.SetOptions(options);
		Type(engine, "zongguo");
		Expect("fuzzy z=zh", First(engine), "中国");
		engine.ProcessKey(Key::kEscape, 0, 0);
		Type(engine, "zhanxiang");
		std::vector<std::string> page = engine.PageCandidates();
		bool found = false;
		for (const std::string& candidate : page)
			found = found || candidate == "长相";
		Expect("fuzzy an=ang", found ? "yes" : "no", "yes");
		engine.ProcessKey(Key::kEscape, 0, 0);
		engine.SetOptions(Options());
		Type(engine, "zongguo");
		Expect("fuzzy off", First(engine) == "中国" ? "same" : "differs",
			"differs");
	}

	// The bigram model, and the word before as context.
	ExpectFirst("full", Script::kSimplified, "wozaixuexizhongwen",
		"我在学习中文");
	// Pairs reach into the lexicon's phrases (去看电影 ends in 电影), and 吧
	// ends sentences where 把 doesn't.
	ExpectFirst("full", Script::kSimplified, "womenzhoumoqukandianyingba",
		"我们周末去看电影吧");
	// z before an h that is an initial too.
	ExpectFirst("full", Script::kSimplified, "zhrmghg", "中华人民共和国");
	{
		// The next best sentence comes second.
		Engine engine;
		engine.Load(sData);
		Type(engine, "shouyintaizainali");
		std::vector<std::string> page = engine.PageCandidates();
		Expect("second sentence", page.size() > 1 ? page[1] : "",
			"收银台在哪里");
		// Picking it commits it whole.
		engine.SelectOnPage(1);
		Expect("second sentence picked", engine.TakeCommit(), "收银台在哪里");
	}
	{
		Engine engine;
		engine.Load(sData);
		Type(engine, "zhongguo ");
		engine.TakeCommit();
		// After 中国, "ren" puts 人 first.
		Type(engine, "ren");
		Expect("context", First(engine), "人");
		engine.ProcessKey(Key::kEscape, 0, 0);
		// The text before the cursor is context too: its last word.
		engine.SetContextText("这件");
		Type(engine, "shi");
		Expect("field context 这件", First(engine), "事");
		engine.ProcessKey(Key::kEscape, 0, 0);
		engine.SetContextText("警方正在调查这起");
		Type(engine, "shijian");
		Expect("field context 这起", First(engine), "事件");
		engine.ProcessKey(Key::kEscape, 0, 0);
		engine.ClearContext();
		Type(engine, "shi");
		Expect("no context", First(engine), "是");
		engine.ProcessKey(Key::kEscape, 0, 0);
	}

	// The AI's pick moves first when it's clearly better, unless the
	// candidates changed since it was asked.
	{
		Engine engine;
		engine.Load(sData);
		engine.SetContextText("我妹妹说");
		Type(engine, "ta");
		Engine::AIQuery query = engine.AIPickQuery();
		Expect("pick prompt", query.prompt, "我妹妹说");
		std::vector<double> scores(query.options.size(), -9);
		scores[0] = -5;
		for (size_t i = 0; i < query.options.size(); i++) {
			if (query.options[i] == "她")
				scores[i] = -1;
		}
		Expect("pick taken", engine.SetAIPickResult(query, scores) ? "yes" : "no",
			"yes");
		Expect("picked first", First(engine), "她");
		Expect("pick marked", std::to_string(engine.AIIndexOnPage()), "0");
		Expect("stale pick", engine.SetAIPickResult(query, scores) ? "yes" : "no",
			"no");
		Expect("no second question", engine.AIPickQuery().options.empty()
			? "yes" : "no", "yes");
		engine.ProcessKey(Key::kEscape, 0, 0);

		// A narrow lead leaves the engine's order.
		Type(engine, "ta");
		query = engine.AIPickQuery();
		scores.assign(query.options.size(), -9);
		scores[0] = -2;
		scores[1] = -1.5;
		Expect("narrow lead", engine.SetAIPickResult(query, scores) ? "yes" : "no",
			"no");
		// Only candidates that spell all the keys are options.
		Type(engine, "de");
		bool partial = false;
		for (const std::string& option : engine.AIPickQuery().options)
			partial = partial || option == "他";
		Expect("only whole spellings", partial ? "yes" : "no", "no");
		engine.ProcessKey(Key::kEscape, 0, 0);
	}

	// After a commit, the AI's completion is offered; Tab takes it, another
	// key drops it.
	{
		Engine engine;
		engine.Load(sData);
		engine.ClearContext();
		Type(engine, "zhuni shengri ");
		engine.TakeCommit();
		std::string prompt = engine.AICompletionQuery();
		Expect("completion prompt", prompt, "祝你生日");
		Expect("completion taken", engine.SetAICompletion(prompt, "快乐")
			? "yes" : "no", "yes");
		Expect("suggestion", engine.Suggestion(), "快乐");
		engine.ProcessKey(Key::kTab, 0, 0);
		Expect("tab commits it", engine.TakeCommit(), "快乐");
		Expect("context grows", engine.ContextText(), "祝你生日快乐");
		engine.SetAICompletion(engine.AICompletionQuery(), "啊");
		Type(engine, "n");
		Expect("a key drops it", engine.Suggestion(), "");
		engine.ProcessKey(Key::kEscape, 0, 0);
		Expect("stale completion", engine.SetAICompletion("祝你", "好")
			? "yes" : "no", "no");
		Type(engine, ",");
		engine.TakeCommit();
		Expect("not after punctuation", engine.AICompletionQuery(), "");
		engine.ProcessKey(Key::kReturn, 0, 0);
		Expect("return clears context", engine.ContextText(), "");
	}

	// English among the candidates.
	{
		Engine engine;
		engine.Load(sData);
		Type(engine, "hello");
		std::vector<std::string> page = engine.PageCandidates();
		Expect("hello second", page.size() > 1 ? page[1] : "", "hello");
		engine.ProcessKey(Key::kEscape, 0, 0);
		Type(engine, "computer");
		Expect("computer first", First(engine), "computer");
	}

	// A cloud answer goes second and can be learned.
	{
		Engine engine;
		engine.Load(sData);
		Type(engine, "shishishishi");
		std::string query = engine.CloudQuery();
		Expect("cloud query", query, "shishishishi");
		engine.SetCloudResult(query, "实时事实");
		std::vector<std::string> page = engine.PageCandidates();
		Expect("cloud second", page.size() > 1 ? page[1] : "", "实时事实");
		Expect("cloud marked", std::to_string(engine.CloudIndexOnPage()), "1");
		Expect("stale answer", engine.SetCloudResult("nihao", "你好")
			? "taken" : "ignored", "ignored");
	}

	// Ctrl+Delete forgets a learned word.
	{
		Engine engine;
		engine.Load(sData);
		char path[] = "/tmp/haiku-pinyin-forget-XXXXXX";
		close(mkstemp(path));
		engine.SetUserDictionary(path);
		Type(engine, "shi");
		std::string fifth = engine.PageCandidates()[4];
		engine.SelectOnPage(4);
		engine.TakeCommit();
		engine.ClearContext();
		Type(engine, "shi");
		Expect("learned", First(engine), fifth);
		engine.ProcessKey(Key::kDelete, 0, kControl);
		Expect("forgotten", First(engine) == fifth ? "kept" : "forgotten",
			"forgotten");
		unlink(path);
	}

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
		// A digit that picks a candidate isn't typed: 你好。 not 你好.
		Type(engine, "nihao1.");
		Expect("period after a pick", engine.TakeCommit(), "你好。");
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

	// v-mode: numbers, sums, dates, codes.
	{
		Expect("v 0", ChineseNumber("0", false), "零");
		Expect("v 10", ChineseNumber("10", false), "十");
		Expect("v 15", ChineseNumber("15", true), "壹拾伍");
		Expect("v 1001", ChineseNumber("1001", false), "一千零一");
		Expect("v 1010", ChineseNumber("1010", false), "一千零一十");
		Expect("v 10100", ChineseNumber("10100", false), "一万零一百");
		Expect("v 100000", ChineseNumber("100000", false), "十万");
		Expect("v 100000001", ChineseNumber("100000001", false), "一亿零一");
		Expect("v 120034005", ChineseNumber("120034005", true),
			"壹亿贰仟零叁万肆仟零伍");
		Expect("v too long", ChineseNumber("12345678901234567", false), "");

		auto first = [](const char* layout, const std::string& keys) {
			Engine engine;
			engine.Load(sData);
			Options options;
			options.layout = layout;
			engine.SetOptions(options);
			// 2026-11-07 (a Saturday), noon UTC: the same day everywhere
			// a test is likely to run.
			engine.SetTime(1794052800);
			for (char key : keys) {
				engine.ProcessKey(Key::kCharacter,
					static_cast<unsigned char>(key), 0);
			}
			std::vector<std::string> page = engine.PageCandidates();
			std::string all;
			for (const std::string& candidate : page)
				all += (all.empty() ? "" : " ") + candidate;
			return all;
		};
		Expect("v123", first("full", "v123"),
			"一百二十三 壹佰贰拾叁 壹佰贰拾叁元整 一二三 CXXIII");
		Expect("v12.05", first("full", "v12.05"),
			"十二点零五 壹拾贰点零伍 壹拾贰元零伍分");
		Expect("v0.5", first("full", "v0.5"), "零点五 零点伍 伍角");
		Expect("v sum", first("full", "v(1+2)*3/4"), "2.25 (1+2)*3/4=2.25");
		Expect("v percent", first("full", "v20%"), "0.2 20%=0.2");
		Expect("v date", first("full", "v2026.11.7"),
			"2026年11月7日 二〇二六年十一月七日 2026-11-07 2026年11月7日 星期六");
		Expect("v xq", first("full", "vxq"), "星期六 周六 礼拜六");
		Expect("v jt", first("full", "vjt").substr(0, 11), "→ ← ↑");
		// Not a code: pinyin and English after all.
		Expect("v english", first("full", "very").substr(0, 4), "very");
		// In shuangpin, v is zh and Shift+V is v-mode.
		Expect("V shuangpin", first("pyjj", "V12"), "十二 壹拾贰 壹拾贰元整 一二 ⑫ XII");

		Engine engine;
		engine.Load(sData);
		Type(engine, "v12");
		Expect("v letters pick", engine.PickKeys().substr(0, 3), "abc");
		engine.ProcessKey(Key::kCharacter, 'b', 0);
		Expect("v pick b", engine.TakeCommit(), "壹拾贰");
		Type(engine, "vjt");
		Expect("v digits pick", engine.PickKeys().substr(0, 3), "123");
		engine.ProcessKey(Key::kCharacter, '2', 0);
		Expect("v pick 2", engine.TakeCommit(), "←");
		Type(engine, "v1");
		engine.ProcessKey(Key::kBackspace, 0, 0);
		engine.ProcessKey(Key::kBackspace, 0, 0);
		Expect("v backspace ends it", engine.Composing() ? "composing" : "done",
			"done");
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
