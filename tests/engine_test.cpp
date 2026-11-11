// Engine tests: engine_test <data directory>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include "../engine/Engine.h"
#include "../engine/Report.h"
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


static std::string
TempDirectory()
{
	char path[] = "/tmp/haiku-pinyin-dir-XXXXXX";
	return mkdtemp(path) != nullptr ? path : "/tmp";
}


static void
WriteFile(const std::string& path, const std::string& contents)
{
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file << contents;
}


static std::string
ReadFile(const std::string& path)
{
	std::ifstream file(path, std::ios::binary);
	std::ostringstream contents;
	contents << file.rdbuf();
	return contents.str();
}


static void
RemoveDirectory(const std::string& path)
{
	std::string command = "rm -rf '" + path + "'";
	if (system(command.c_str()) != 0)
		printf("couldn't remove %s\n", path.c_str());
}


// Custom phrases: both syntaxes, placement, several per code, nonsense
// lines, and reading the file again when it changes.
static void
TestPhrases()
{
	{
		Phrases phrases;
		phrases.Parse(
			"# a comment\n"
			"; Sogou's comment\n"
			"dz,1=北京市海淀区\r\n"
			"yx,2=work@example.com\n"
			"yx,1=me@example.com\n"
			"YX,5=upper case code\n"
			"Rime 的短语\trime\t3\n"
			"再一条\trime\t9\n"
			"没有权重\tqz\n"
			"\n"
			"no equals sign\n"
			"dz,x=bad position\n"
			"dz=no position\n"
			",1=no code\n"
			"d2,1=a digit in the code\n"
			"rq,1=#$year年$month月$day日\n"
			"\tno text\n"
			"\xff\xfe\x01garbage\n"
			"=,=,=\n"
			"kong,1=\n");
		Expect("phrases parsed", std::to_string(phrases.Size()), "7");
		int ignored = 0;
		Phrases::Read("a,1=x\nnonsense\n\tnope\nb,1=\n\n", &ignored);
		Expect("phrases ignored", std::to_string(ignored), "3");
		std::vector<Phrases::Phrase> found = phrases.Find("yx");
		std::string texts;
		for (const Phrases::Phrase& phrase : found)
			texts += phrase.text + "|";
		Expect("phrases by position", texts,
			"me@example.com|work@example.com|upper case code|");
		found = phrases.Find("rime");
		Expect("rime weights", found.size() == 2 ? found[0].text : "", "再一条");
		Expect("phrase no match", std::to_string(phrases.Find("d").size()), "0");

		// Several lines: code,position= alone, then the text.
		phrases.Parse("dz,1=\r\n北京市\r\n海淀区\r\n\r\n; a comment\r\nyx,1=me\r\n");
		found = phrases.Find("dz");
		Expect("phrase of lines", found.size() == 1 ? found[0].text : "",
			"北京市\n海淀区");
		Expect("after a phrase of lines",
			std::to_string(phrases.Find("yx").size()), "1");
		// Rime's codes may spell syllables apart; "# no comment" makes #
		// text.
		phrases.Parse("一个\tyi ge\n# no comment\n#话题\tht\n");
		Expect("rime spaced code", std::to_string(phrases.Find("yige").size()),
			"1");
		found = phrases.Find("ht");
		Expect("rime no comment", found.size() == 1 ? found[0].text : "",
			"#话题");

		// UTF-16 with a BOM, as Sogou writes it.
		std::string utf16 = "\xff\xfe";
		for (char c : std::string("ab,1=x"))
			utf16 += std::string(1, c) + '\0';
		utf16 += std::string("\x2d\x4e\r\0\n\0", 6);	// 中\r\n
		phrases.Parse(utf16);
		found = phrases.Find("ab");
		Expect("utf-16 phrases", found.size() == 1 ? found[0].text : "", "x中");

		phrases.Parse(Phrases::Example());
		Expect("example is all comments", std::to_string(phrases.Size()), "0");
	}

	std::string directory = TempDirectory();
	std::string file = directory + "/phrases.txt";
	{
		Engine engine;
		engine.Load(sData);
		engine.SetUserDictionary(directory + "/user.dict");
		Expect("phrases next to user.dict", engine.PhrasesFile(), file);
		Expect("phrases file created",
			engine.CustomPhrases().CreateFile() ? "yes" : "no", "yes");
		Expect("phrases file example", ReadFile(file), Phrases::Example());

		WriteFile(file, "dz,1=北京市海淀区\nyx,1=me@example.com\n"
			"yx,3=work@example.com\nyx,9999=last\nnihao,3=你好\n");
		Type(engine, "dz");
		Expect("phrase first", First(engine), "北京市海淀区");
		engine.ProcessKey(Key::kSpace, 0, 0);
		Expect("phrase commits", engine.TakeCommit(), "北京市海淀区");

		Type(engine, "yx");
		std::vector<std::string> page = engine.PageCandidates();
		Expect("phrase 1 of 3", page.size() > 0 ? page[0] : "", "me@example.com");
		Expect("phrase 3 of 3", page.size() > 2 ? page[2] : "",
			"work@example.com");
		Expect("phrase 2 of 3 not a phrase", page.size() > 1 && page[1] != "last"
			&& page[1] != "work@example.com" ? "no" : "yes", "no");
		while (engine.HasNextPage())
			engine.ProcessKey(Key::kPageDown, 0, 0);
		page = engine.PageCandidates();
		Expect("phrase past the end goes last", page.empty() ? "" : page.back(),
			"last");
		engine.ProcessKey(Key::kEscape, 0, 0);

		// The same text elsewhere moves to the phrase's place.
		Type(engine, "nihao");
		page = engine.PageCandidates();
		Expect("phrase moves its text", page.size() > 2 ? page[2] : "", "你好");
		int count = 0;
		for (const std::string& candidate : page)
			count += candidate == "你好";
		Expect("phrase not twice", std::to_string(count), "1");
		engine.ProcessKey(Key::kEscape, 0, 0);

		// Only the whole of the keys.
		Type(engine, "dzh");
		Expect("phrase exact", First(engine) == "北京市海淀区" ? "shown" : "not",
			"not");
		engine.ProcessKey(Key::kEscape, 0, 0);

		// As written, even in traditional output.
		Options options;
		options.script = Script::kTraditional;
		engine.SetOptions(options);
		Type(engine, "dz");
		Expect("phrase verbatim", First(engine), "北京市海淀区");
		engine.ProcessKey(Key::kCharacter, '1', 0);
		Expect("phrase verbatim commit", engine.TakeCommit(), "北京市海淀区");

		// Shuangpin: the code is the keys as typed, whatever they spell
		// (dz is 段 in 拼音加加).
		for (const char* layout : {"pyjj", "xiaohe", "mspy"}) {
			options = Options();
			options.layout = layout;
			engine.SetOptions(options);
			Type(engine, "dz");
			Expect((std::string("phrase in ") + layout).c_str(), First(engine),
				"北京市海淀区");
			engine.ProcessKey(Key::kEscape, 0, 0);
		}
		engine.SetOptions(Options());

		// Changed on disk: read again as the next composition starts.
		WriteFile(file, "dz,2=上海市徐汇区\n# yx is gone\n");
		Type(engine, "dz");
		page = engine.PageCandidates();
		Expect("phrase reloaded", page.size() > 1 ? page[1] : "", "上海市徐汇区");
		engine.ProcessKey(Key::kEscape, 0, 0);
		Type(engine, "yx");
		Expect("phrase removed", First(engine) == "me@example.com" ? "kept"
			: "gone", "gone");
		engine.ProcessKey(Key::kEscape, 0, 0);

		// Removed: no phrases, no crash.
		unlink(file.c_str());
		Type(engine, "dz");
		Expect("phrases file gone", First(engine) == "上海市徐汇区" ? "kept"
			: "gone", "gone");
		engine.ProcessKey(Key::kEscape, 0, 0);
	}
	RemoveDirectory(directory);
}


static std::string
Imported(Engine& engine, const std::string& directory, const std::string& name,
	const std::string& contents)
{
	std::string path = directory + "/" + name;
	WriteFile(path, contents);
	Engine::ImportResult result = engine.ImportWords(path);
	return result.format + " " + std::to_string(result.words) + " "
		+ std::to_string(result.phrases) + " " + std::to_string(result.skipped)
		+ (result.problem == ImportProblem::kNone ? "" : " error");
}


// What a user dictionary file holds: "词 pin yin count" lines, sorted.
static std::string
Words(const std::string& path)
{
	Engine engine;
	engine.Load(sData);
	engine.SetUserDictionary(path);
	std::vector<std::string> lines;
	const UserDict& user = engine.UserDictionary();
	for (uint32_t i = 0; i < user.Size(); i++) {
		std::string line = user.At(i).text;
		for (SyllableId id : user.At(i).key)
			line += " " + std::to_string(id);
		line += " " + std::to_string(user.At(i).count);
		lines.push_back(line);
	}
	std::sort(lines.begin(), lines.end());
	std::string all;
	for (const std::string& line : lines)
		all += line + "\n";
	return all;
}


// Learned words out and back in, other input methods' files in, and a
// running engine taking up a change made by another.
static void
TestImport()
{
	std::string directory = TempDirectory();
	std::string userPath = directory + "/user.dict";
	std::string fifth;
	{
		Engine engine;
		engine.Load(sData);
		engine.SetUserDictionary(userPath);
		Type(engine, "zhongguoren ");
		Type(engine, "shi");
		fifth = engine.PageCandidates()[4];
		engine.SelectOnPage(4);
		Type(engine, "shi");
		engine.SelectOnPage(0);		// the same word, first now
		engine.TakeCommit();
		engine.SaveUserDictionary();

		int count = 0;
		Expect("export", engine.ExportWords(directory + "/words.txt", &count)
			? std::to_string(count) : "failed", "2");
		std::string exported = ReadFile(directory + "/words.txt");
		Expect("export line", exported.find(fifth + "\tshi\t2\n")
			!= std::string::npos ? "yes" : exported, "yes");
		Expect("export line 2", exported.find("中国人\tzhong guo ren\t1\n")
			!= std::string::npos ? "yes" : exported, "yes");
	}
	{
		// Into an empty dictionary: the same words, then merged, the higher
		// count winning.
		std::string other = directory + "/other";
		mkdir(other.c_str(), 0755);
		Engine engine;
		engine.Load(sData);
		engine.SetUserDictionary(other + "/user.dict");
		Engine::ImportResult result
			= engine.ImportWords(directory + "/words.txt");
		Expect("import ours", result.format + " " + std::to_string(result.words),
			"Haiku Pinyin 2");
		Expect("round trip", Words(other + "/user.dict"), Words(userPath));
		Type(engine, "shi");
		Expect("imported word first", First(engine), fifth);
		engine.ProcessKey(Key::kEscape, 0, 0);
		Imported(engine, directory, "more.txt",
			fifth + "\tshi\t1\n中国人\tzhong guo ren\t7\n");
		std::string words = Words(other + "/user.dict");
		Expect("merge keeps higher", words.find(" 2\n") != std::string::npos
			&& words.find(" 7\n") != std::string::npos ? "yes" : words, "yes");

		// Sogou's words, with pinyin or, when it's no use, from the lexicon.
		Expect("sogou words", Imported(engine, directory, "sogou.txt",
			"'ni'hao 你好\r\n'zhong'guo 中国 3\r\n'xx'yy 你好啊\r\n"
			"'a ☃☃\r\nnot a word line\r\n"), "Sogou 3 0 2");
		Expect("lexicon spelling", Words(other + "/user.dict").find("你好啊")
			!= std::string::npos ? "yes" : "no", "yes");

		// Rime's snapshot: deleted (c<1) entries skipped.
		Expect("rime userdb", Imported(engine, directory,
			"luna_pinyin.userdb.txt",
			"# Rime user dictionary\n#@/db_name\tluna_pinyin.userdb\n"
			"an zhuang \t安装\tc=2 d=1.9415 t=39\n"
			"lie biao \t列表\tc=0 d=0.09 t=39\n"
			"shi da fu wo \t士大夫我\tc=-1 d=0 t=219\n"),
			"Rime user dictionary 1 0 2");

		// Phrases: Rime's custom_phrase.txt, Sogou's, the Mac's.
		Expect("rime phrases", Imported(engine, directory, "custom_phrase.txt",
			"# Rime table\n#@/db_name\tcustom_phrase.txt\n"
			"又\tu\t2\n有\tu\t3\nAmazon\tamazon\n一个\tyi ge\n"),
			"Rime custom phrases 0 4 0");
		// Without that header, a code that spells the text is a word.
		Expect("rime phrases or words", Imported(engine, directory, "mixed.txt",
			"一个\tyi ge\nAmazon\tamazon\n"), "mixed 1 1 0");
		Expect("sogou phrases", Imported(engine, directory, "PhraseEdit.txt",
			"\xef\xbb\xbf; 搜狗输入法--自定义短语配置文件\r\n\r\n"
			"dz,1=北京市海淀区\r\nss,1=#$year年\r\nones,2=\r\nline one\r\n"
			"line two\r\nb,1=吧\r\n"),
			"Sogou custom phrases 0 3 1");
		Expect("mac phrases", Imported(engine, directory, "Text.plist",
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\">\n"
			"<array>\n\t<dict>\n\t\t<key>phrase</key>\n\t\t<string>On my way!"
			"</string>\n\t\t<key>shortcut</key>\n\t\t<string>omw</string>\n"
			"\t</dict>\n\t<dict>\n\t\t<key>phrase</key>\n\t\t<string>→</string>\n"
			"\t\t<key>shortcut</key>\n\t\t<string>:-&gt;:</string>\n\t</dict>\n"
			"\t<dict>\n\t\t<key>phrase</key>\n\t\t<string>R&amp;D &#x4e2d;\n"
			"two</string>\n\t\t<key>shortcut</key>\n\t\t<string>RD</string>\n"
			"\t</dict>\n</array>\n</plist>\n"),
			"text replacements 0 2 1");
		Expect("same phrases again", Imported(engine, directory, "Text2.plist",
			ReadFile(directory + "/Text.plist")), "text replacements 0 2 1");
		Phrases phrases;
		phrases.SetPath(other + "/phrases.txt");
		Expect("imported phrases", std::to_string(phrases.Size()), "9");
		std::vector<Phrases::Phrase> found = phrases.Find("rd");
		Expect("plist text", found.size() == 1 ? found[0].text : "", "R&D 中\ntwo");
		found = phrases.Find("u");
		Expect("rime weight order", found.size() == 2 ? found[0].text : "", "有");
		Type(engine, "omw");
		Expect("imported phrase typed", First(engine), "On my way!");
		engine.ProcessKey(Key::kEscape, 0, 0);
		Type(engine, "ones");
		std::vector<std::string> page = engine.PageCandidates();
		Expect("phrase label", page.size() > 1 ? page[1] : "",
			"line one⏎line two");
		engine.ProcessKey(Key::kCharacter, '2', 0);
		Expect("phrase of lines commits", engine.TakeCommit(),
			"line one\nline two");

		// Nonsense: never a crash.
		Expect("binary plist", Imported(engine, directory, "b.plist",
			std::string("bplist00\x01\x02", 10)), " 0 0 0 error");
		Expect("gbk", Imported(engine, directory, "gbk.txt",
			"'ni'hao \xc4\xe3\xba\xc3\r\n"), " 0 0 0 error");
		Expect("empty", Imported(engine, directory, "empty.txt", ""), " 0 0 0");
		Expect("garbage", Imported(engine, directory, "garbage.txt",
			std::string("\x00\x01\t\t\t\n<dict><key>", 21)).substr(0, 1), " ");
		Expect("missing", engine.ImportWords(directory + "/none").problem
			== ImportProblem::kUnreadable ? "unreadable" : "other", "unreadable");
	}
	{
		// Another process (hpcli, the Mac's settings window) imports while
		// this one runs: it reads the file again as the next composition
		// starts, keeping what it learned since its last save.
		Engine running;
		running.Load(sData);
		running.SetUserDictionary(userPath);
		Type(running, "ta");
		std::string third = running.PageCandidates()[2];
		running.SelectOnPage(2);
		running.TakeCommit();

		Engine other;
		other.Load(sData);
		other.SetUserDictionary(userPath);
		std::string imported = Imported(other, directory, "late.txt",
			"已经\tyi jing\t9\n");
		Expect("import elsewhere", imported, "Haiku Pinyin 1 0 0");
		Type(running, "yijing");
		Expect("taken up", First(running), "已经");
		running.ProcessKey(Key::kEscape, 0, 0);
		running.SaveUserDictionary();
		std::string words = Words(userPath);
		Expect("kept its own", words.find(third + " ") != std::string::npos
			? "yes" : words, "yes");
		Expect("kept the import", words.find("已经 ") != std::string::npos
			? "yes" : words, "yes");
	}
	RemoveDirectory(directory);
}


static bool
Has(const std::string& text, const std::string& part)
{
	return text.find(part) != std::string::npos;
}


// #pragma mark - sync


// A machine: its own folder with user.dict (and phrases.txt, sync-id), and
// an engine on it syncing through shared.
struct Machine {
	std::string directory;
	Engine engine;

	Machine(const std::string& root, const char* name,
		const std::string& shared)
		:
		directory(root + "/" + name)
	{
		mkdir(directory.c_str(), 0755);
		engine.Load(sData);
		engine.SetUserDictionary(directory + "/user.dict");
		engine.SetSyncFolder(shared);
	}

	// Picks the candidate at index for keys, and commits it.
	void Pick(const std::string& keys, int index)
	{
		Type(engine, keys);
		engine.SelectOnPage(index);
		engine.TakeCommit();
	}

	std::string FirstFor(const std::string& keys)
	{
		Type(engine, keys);
		std::string first = First(engine);
		engine.ProcessKey(Key::kEscape, 0, 0);
		return first;
	}

	SyncReport Round() { return engine.SyncNow(); }
	std::string Words() const { return ::Words(directory + "/user.dict"); }
};


static uint32_t
CountOf(const UserDict& user, const std::string& text)
{
	for (uint32_t i = 0; i < user.Size(); i++) {
		if (user.At(i).text == text)
			return user.At(i).count;
	}
	return 0;
}


static void
SetModified(const std::string& path, time_t when)
{
	struct utimbuf times;
	times.actime = when;
	times.modtime = when;
	utime(path.c_str(), &times);
}


// Learned words and custom phrases through a shared folder: the join's
// rules, rounds that change nothing, forgotten words, half-written files,
// two machines agreeing, and the running engine's worker.
static void
TestSync()
{
	{
		// The join: higher count, lower cost, newer time; the same either
		// way round, and twice is once.
		std::vector<SyllableId> key = {1, 2};
		UserDict a, b;
		a.Add(key, "甲", 9.0f, 3, 100);
		b.Add(key, "甲", 7.0f, 2, 200);
		b.Add({3}, "乙", 5.0f, 1, 150);
		UserDict ab = a, ba = b;
		Expect("join changes", std::to_string(ab.Join(b)), "2");
		ba.Join(a);
		Expect("join count", std::to_string(CountOf(ab, "甲")), "3");
		Expect("join cost", std::to_string(int(ab.At(0).cost)), "7");
		Expect("join time", std::to_string(ab.At(0).lastUsed), "200");
		Expect("join either way", std::to_string(ba.Size()) + " "
			+ std::to_string(CountOf(ba, "甲")) + " "
			+ std::to_string(int(ba.At(0).cost)), "2 3 7");
		Expect("join twice", std::to_string(ab.Join(b)), "0");

		// Forgotten after it was last learned: it goes, and stays gone
		// when an older copy comes back; learned again after, it returns.
		UserDict forgot = ab;
		forgot.Add(key, "甲", 0, 0, 300);
		Expect("forgotten", std::to_string(CountOf(forgot, "甲")), "0");
		UserDict stale = a;
		Expect("forget wins", std::to_string(stale.Join(forgot)) + " "
			+ std::to_string(CountOf(stale, "甲")), "3 0");
		stale.Join(a);
		Expect("old copy stays out", std::to_string(CountOf(stale, "甲")), "0");
		UserDict later;
		later.Add(key, "甲", 9.0f, 1, 400);
		stale.Join(later);
		Expect("learned again", std::to_string(CountOf(stale, "甲")), "1");
		Expect("forget kept", std::to_string(stale.ForgottenWords().size()),
			"1");

		// A forget taken in from a machine whose clock runs ahead still
		// comes before learning the word here afterward.
		UserDict here;
		here.SetTime(1000);
		here.Learn(key, "甲", 9.0f);
		UserDict ahead;
		ahead.Add(key, "甲", 0, 0, 5000);
		here.Join(ahead);
		Expect("ahead forgets", std::to_string(CountOf(here, "甲")), "0");
		here.Learn(key, "甲", 9.0f);
		UserDict elsewhere;
		elsewhere.Join(ahead);
		elsewhere.Join(here);
		Expect("learned after a forget from ahead",
			std::to_string(CountOf(elsewhere, "甲")), "1");
	}

	std::string root = TempDirectory();
	std::string shared = root + "/shared";
	mkdir(shared.c_str(), 0755);
	std::string folder = shared + "/haiku-pinyin";
	{
		Machine a(root, "a", shared), b(root, "b", shared);
		// A fifth candidate learned on one machine is first on the other.
		Type(a.engine, "shi");
		std::string fifth = a.engine.PageCandidates()[4];
		a.engine.ProcessKey(Key::kEscape, 0, 0);
		a.Pick("shi", 4);
		a.Pick("shi", 0);
		a.Pick("zhongguoren ", 0);
		b.Pick("nihao ", 0);
		SyncReport report = a.Round();
		Expect("sync report", report.error + std::to_string(report.sent), "2");
		report = b.Round();
		Expect("sync report b", report.error + std::to_string(report.received)
			+ " " + std::to_string(report.changed) + " "
			+ std::to_string(report.machines), "2 2 1");
		a.Round();
		Expect("learned elsewhere is first", b.FirstFor("shi"), fifth);
		Expect("both ways", a.FirstFor("nihao"), "你好");
		Expect("converged", a.Words(), b.Words());
		std::string words = a.Words();
		Expect("each its own file", std::to_string(
			Has(ReadFile(folder + "/" + ReadFile(a.directory + "/sync-id")
				.substr(0, 16) + ".words"), "中国人\tzhong guo ren\t1\t")),
			"1");

		// Rounds and more rounds: nothing counted twice.
		for (int i = 0; i < 4; i++) {
			a.Round();
			b.Round();
		}
		Expect("no double counting", b.Words(), words);
		Expect("count stays", std::to_string(CountOf(
			b.engine.UserDictionary(), fifth)), "2");
		// The same word learned on both: the higher count, not the sum.
		a.Pick("shi", 0);
		b.Pick("shi", 0);
		b.Pick("shi", 0);
		a.Round();
		b.Round();
		a.Round();
		Expect("higher count", std::to_string(CountOf(
			a.engine.UserDictionary(), fifth)) + " " + std::to_string(CountOf(
			b.engine.UserDictionary(), fifth)), "4 4");

		// Ctrl+Delete on one: gone on the other, and a third machine's old
		// copy doesn't bring it back.
		Machine c(root, "c", shared);
		c.Round();
		Expect("third has it", std::to_string(CountOf(
			c.engine.UserDictionary(), fifth)), "4");
		c.engine.SetSyncFolder("");
		Type(a.engine, "shi");
		Expect("forgetting", First(a.engine), fifth);
		a.engine.ProcessKey(Key::kDelete, 0, kControl);
		a.engine.ProcessKey(Key::kEscape, 0, 0);
		a.Round();
		b.Round();
		Expect("forgotten elsewhere", std::to_string(CountOf(
			b.engine.UserDictionary(), fifth)), "0");
		Expect("forgotten elsewhere, typed", b.FirstFor("shi") != fifth
			? "gone" : "there", "gone");
		c.engine.SetSyncFolder(shared);
		c.Round();
		a.Round();
		b.Round();
		Expect("old copy dropped", std::to_string(CountOf(
			c.engine.UserDictionary(), fifth)) + std::to_string(CountOf(
			a.engine.UserDictionary(), fifth)) + std::to_string(CountOf(
			b.engine.UserDictionary(), fifth)), "000");
		Expect("tombstone written", std::to_string(Has(
			ReadFile(folder + "/" + ReadFile(b.directory + "/sync-id")
				.substr(0, 16) + ".words"), "-\t" + fifth + "\tshi\t")), "1");
		// Learned again afterward, anywhere: back everywhere, from one.
		b.Pick("shi", 4);
		std::string again = b.engine.UserDictionary().Size() > 0
			? std::to_string(CountOf(b.engine.UserDictionary(), fifth)) : "";
		b.Round();
		a.Round();
		c.Round();
		Expect("learned again everywhere", std::to_string(CountOf(
			a.engine.UserDictionary(), fifth)) + std::to_string(CountOf(
			c.engine.UserDictionary(), fifth)), again + again);
		Expect("forgotten survives in user.dict", std::to_string(Has(
			ReadFile(a.directory + "/user.dict"), "0\t")), "1");

		// Half-synced and broken files: no end, garbage lines, zeros, a
		// temporary file. Only the whole file's good lines count.
		WriteFile(folder + "/1111111111111111.words",
			"# machine: half\n安全\tan quan\t50\t1\t9\n");
		WriteFile(folder + "/2222222222222222.words",
			"# machine: garbled\n"
			"安全\tan quan\t7\t1\t9\n"
			"不对\tnot pinyin\t9\t1\t9\n"
			"缺字段\tque\n"
			"负数\tfu shu\t-3\t1\t9\n"
			"\t\t\t\t\n"
			"-\t安心\tan xin\tsoon\n"
			+ std::string("\0\0\0\0\n", 5)
			+ "# end\n");
		WriteFile(folder + "/3333333333333333.words",
			std::string(64, '\0'));
		WriteFile(folder + "/.4444444444444444.words.new",
			"一二\tyi er\t99\t1\t9\n# end\n");
		WriteFile(folder + "/notes.txt", "a\tb\t1\t1\t1\n# end\n");
		report = b.Round();
		Expect("partial files", std::to_string(CountOf(
			b.engine.UserDictionary(), "安全")) + " " + std::to_string(CountOf(
			b.engine.UserDictionary(), "不对")) + std::to_string(CountOf(
			b.engine.UserDictionary(), "负数")) + std::to_string(CountOf(
			b.engine.UserDictionary(), "一二")), "7 000");
		Expect("partial files read", std::to_string(report.machines), "3");
		// The half file, finished by the sync tool.
		WriteFile(folder + "/1111111111111111.words",
			"# machine: half\n安全\tan quan\t50\t1\t9\n# end\n");
		b.Round();
		Expect("finished file", std::to_string(CountOf(
			b.engine.UserDictionary(), "安全")), "50");
	}
	RemoveDirectory(root);

	// Custom phrases: the newest edit wins, as a whole file.
	root = TempDirectory();
	shared = root + "/shared";
	mkdir(shared.c_str(), 0755);
	folder = shared + "/haiku-pinyin";
	{
		Machine a(root, "a", shared), b(root, "b", shared);
		std::string aPhrases = a.directory + "/phrases.txt";
		std::string bPhrases = b.directory + "/phrases.txt";
		// A new file's example never replaces anyone's phrases.
		b.engine.CustomPhrases().CreateFile();
		WriteFile(aPhrases, "dz,1=北京市海淀区\n");
		SetModified(aPhrases, 1000000000);
		a.Round();
		SyncReport report = b.Round();
		Expect("phrases taken", ReadFile(bPhrases), "dz,1=北京市海淀区\n");
		Expect("phrases from", report.phrasesFrom.empty() ? "nobody" : "a",
			"a");
		Expect("example not kept", std::to_string(
			FileStamp::Of(bPhrases + ".bak").size), "-1");
		Expect("phrase typed", b.FirstFor("dz"), "北京市海淀区");
		a.Round();
		Expect("example didn't win", ReadFile(aPhrases), "dz,1=北京市海淀区\n");

		// Edited on b later: a takes it, and keeps its own as .bak.
		WriteFile(bPhrases, "dz,1=北京市海淀区\nyx,1=me@example.com\n");
		SetModified(bPhrases, 1000000100);
		WriteFile(aPhrases, "dz,1=上海\n");
		SetModified(aPhrases, 1000000050);
		b.Round();
		a.Round();
		Expect("newest wins", ReadFile(aPhrases),
			"dz,1=北京市海淀区\nyx,1=me@example.com\n");
		Expect("replaced kept", ReadFile(aPhrases + ".bak"), "dz,1=上海\n");
		b.Round();
		Expect("older lost", ReadFile(bPhrases),
			"dz,1=北京市海淀区\nyx,1=me@example.com\n");
		Expect("phrase typed after", a.FirstFor("yx"), "me@example.com");

		// A phrases file half there doesn't count.
		WriteFile(folder + "/5555555555555555.phrases",
			"# machine: half\n# edited: 2000000000\n# bytes: 40\n# ---\n"
			"dz,1=半个\n");
		a.Round();
		Expect("half phrases", ReadFile(aPhrases),
			"dz,1=北京市海淀区\nyx,1=me@example.com\n");

		// Edited on a after it took b's, with a clock behind b's: still
		// the newer edit.
		WriteFile(aPhrases, "dz,1=深圳\n");
		SetModified(aPhrases, 1000000010);
		a.Round();
		b.Round();
		Expect("edited after, clock behind", ReadFile(bPhrases), "dz,1=深圳\n");
		a.Round();
		Expect("kept after", ReadFile(aPhrases), "dz,1=深圳\n");
	}
	RemoveDirectory(root);

	// The setting, and a running engine's worker: no SyncNow, only typing.
	root = TempDirectory();
	shared = root + "/shared";
	mkdir(shared.c_str(), 0755);
	{
		std::string aDirectory = root + "/a", bDirectory = root + "/b";
		mkdir(aDirectory.c_str(), 0755);
		mkdir(bDirectory.c_str(), 0755);
		Expect("setting", std::to_string(WriteSyncSetting(
			aDirectory + "/user.dict", shared + "/")), "1");
		WriteSyncSetting(bDirectory + "/user.dict", shared);
		Expect("setting read", ReadSyncSetting(aDirectory + "/user.dict"),
			shared);
		Engine a, b;
		a.Load(sData);
		a.SetUserDictionary(aDirectory + "/user.dict");
		b.Load(sData);
		b.SetUserDictionary(bDirectory + "/user.dict");
		Expect("setting taken", a.SyncFolder(), shared);

		// Up to ten seconds for the workers.
		auto until = [](const std::function<bool()>& done) {
			for (int i = 0; i < 200 && !done(); i++)
				usleep(50000);
			return done();
		};
		auto firstFor = [](Engine& engine, const std::string& keys) {
			Type(engine, keys);
			std::string first = First(engine);
			engine.ProcessKey(Key::kEscape, 0, 0);
			return first;
		};
		// b looks first, and finds only its own file; then once more, so it
		// has seen the folder as it is.
		firstFor(b, "ni");
		folder = shared + "/haiku-pinyin";
		Expect("worker starts", until([&] {
			return FileStamp::Of(folder + "/" + ReadFile(bDirectory
				+ "/sync-id").substr(0, 16) + ".words").size > 0;
		}) ? "yes" : "no", "yes");
		firstFor(b, "ni");
		usleep(300000);
		struct stat before;
		stat(folder.c_str(), &before);

		Type(a, "shi");
		std::string fifth = a.PageCandidates()[4];
		a.SelectOnPage(4);
		a.TakeCommit();
		a.SaveUserDictionary();
		Expect("worker writes", until([&] {
			std::string id = ReadFile(aDirectory + "/sync-id").substr(0, 16);
			return Has(ReadFile(folder + "/" + id + ".words"),
				fifth + "\tshi\t1\t");
		}) ? "yes" : "no", "yes");
		// As rsync -a leaves it: the folder's time put back, to the
		// nanosecond.
#if defined(__APPLE__)
		struct timespec times[2] = {before.st_atimespec, before.st_mtimespec};
#else
		struct timespec times[2] = {before.st_atim, before.st_mtim};
#endif
		utimensat(AT_FDCWD, folder.c_str(), times, 0);
		Expect("worker reads", until([&] {
			return firstFor(b, "shi") == fifth;
		}) ? "yes" : "no", "yes");
		Type(b, "shi");
		b.ProcessKey(Key::kDelete, 0, kControl);
		b.ProcessKey(Key::kEscape, 0, 0);
		Expect("worker forgets", until([&] {
			return firstFor(a, "shi") != fifth;
		}) ? "yes" : "no", "yes");

		WriteSyncSetting(aDirectory + "/user.dict", "");
		firstFor(a, "ni");
		Expect("setting off", a.SyncFolder(), "");
	}
	RemoveDirectory(root);
}


// The bug report link: the template's fields, escaped, and the settings.
static void
TestReport()
{
	Options options;
	options.layout = "xiaohe";
	options.cloud = true;
	options.fuzzy = kFuzzyRetroflex | kFuzzyNasal;
	std::string url = ReportUrl("Linux, IBus", "0.1.20",
		"Ubuntu 24.04 & GNOME", options);
	static const char* const kParts[] = {
		"/issues/new?template=bug_report.yml&",
		"&platform=Linux%2C%20IBus&",
		"&version=0.1.20&",
		"&system=Ubuntu%2024.04%20%26%20GNOME&",
		// 双拼 · 小鹤, as the template's dropdown has it.
		"&layout=%E5%8F%8C%E6%8B%BC%20%C2%B7%20%E5%B0%8F%E9%B9%A4&",
		"&settings=",
	};
	for (const char* part : kParts)
		Expect(part, Has(url, part) ? part : url, part);
	Expect("report full", ReportLayout("full"), "全拼 Full pinyin");
	Expect("report unknown layout", ReportLayout("nope"), "全拼 Full pinyin");

	std::string settings = ReportSettings(options);
	for (const char* part : {"云候选: on\n",
			"模糊音: z=zh c=ch s=sh, an=ang en=eng in=ing\n",
			"双拼 a o e: aa oo ee"}) {
		Expect(part, Has(settings, part) ? part : settings, part);
	}
	options.layout = "full";
	Expect("report: no a o e in full pinyin",
		Has(ReportSettings(options), "a o e") ? "there" : "not", "not");
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

	TestPhrases();
	TestImport();
	TestSync();
	TestReport();

	printf(sFailures == 0 ? "all passed\n" : "%d failed\n", sFailures);
	return sFailures == 0 ? 0 : 1;
}
