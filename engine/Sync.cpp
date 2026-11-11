#include "Sync.h"

#include <chrono>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include "Phrases.h"

namespace haikupinyin {

static const char* const kSubfolder = "/haiku-pinyin";
static const char* const kEnd = "# end\n";
// After a poke the worker looks every few seconds for a couple of minutes,
// then every minute.
static const std::chrono::seconds kEvery(5);
static const std::chrono::seconds kIdle(60);
static const std::chrono::seconds kActive(120);


// #pragma mark - files


static std::string
Directory(const std::string& path)
{
	size_t slash = path.rfind('/');
	return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}


static bool
ReadAll(const std::string& path, std::string& contents)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return false;
	std::ostringstream stream;
	stream << file.rdbuf();
	contents = stream.str();
	return true;
}


// To a hidden name beside it, then renamed into place: a reader, or the
// sync tool, sees the old file or the new one, never half of one.
static bool
WriteAll(const std::string& path, const std::string& contents)
{
	size_t slash = path.rfind('/');
	std::string temp = path.substr(0, slash + 1) + "."
		+ path.substr(slash + 1) + ".new";
	{
		std::ofstream file(temp, std::ios::binary | std::ios::trunc);
		if (!file)
			return false;
		file << contents;
		if (!file.flush())
			return false;
	}
	if (rename(temp.c_str(), path.c_str()) != 0) {
		unlink(temp.c_str());
		return false;
	}
	return true;
}


static bool
EndsWith(const std::string& text, const std::string& end)
{
	return text.size() >= end.size()
		&& text.compare(text.size() - end.size(), end.size(), end) == 0;
}


static std::vector<std::string>
Fields(const std::string& line)
{
	std::vector<std::string> fields;
	std::string field;
	std::istringstream stream(line);
	while (std::getline(stream, field, '\t'))
		fields.push_back(field);
	return fields;
}


// Digits only, or false.
static bool
Number(const std::string& text, uint64_t& value)
{
	if (text.empty() || text.size() > 19)
		return false;
	value = 0;
	for (char c : text) {
		if (c < '0' || c > '9')
			return false;
		value = value * 10 + uint64_t(c - '0');
	}
	return true;
}


// Text worth taking: no control characters (a file half zeros, say).
static bool
Printable(const std::string& text)
{
	if (text.empty())
		return false;
	for (unsigned char c : text) {
		if (c < 0x20 || c == 0x7f)
			return false;
	}
	return true;
}


static std::string
MachineName()
{
	char name[256] = {};
	if (gethostname(name, sizeof(name) - 1) != 0 || name[0] == '\0')
		return "?";
	std::string text = name;
	if (EndsWith(text, ".local"))
		text.resize(text.size() - 6);
	for (char& c : text) {
		if (static_cast<unsigned char>(c) < 0x20)
			c = ' ';
	}
	return text;
}


// The header's "# machine: …" line.
static std::string
HeaderName(const std::string& contents)
{
	static const std::string kMachine = "# machine: ";
	size_t at = contents.find("\n" + kMachine);
	if (at == std::string::npos)
		return std::string();
	at += 1 + kMachine.size();
	return contents.substr(at, contents.find('\n', at) - at);
}


// #pragma mark - the setting


std::string
SyncSettingFile(const std::string& userDictionary)
{
	return Directory(userDictionary) + "sync-folder";
}


std::string
ReadSyncSetting(const std::string& userDictionary)
{
	std::string contents;
	if (!ReadAll(SyncSettingFile(userDictionary), contents))
		return std::string();
	size_t end = contents.find('\n');
	if (end != std::string::npos)
		contents.resize(end);
	while (!contents.empty() && (contents.back() == '\r'
			|| contents.back() == '/' || contents.back() == ' ')) {
		contents.pop_back();
	}
	return contents;
}


bool
WriteSyncSetting(const std::string& userDictionary, const std::string& folder)
{
	std::string path = SyncSettingFile(userDictionary);
	if (folder.empty())
		return unlink(path.c_str()) == 0 || errno == ENOENT;
	return WriteAll(path, folder + "\n");
}


// #pragma mark - the state


namespace {

struct Config {
	std::string folder;		// <folder>/haiku-pinyin
	std::string user;
	std::string phrases;
	uint32_t generation = 0;
};

// Another machine's phrases.txt, as it published it.
struct PhrasesCopy {
	std::string name;
	uint64_t edited = 0;
	std::string text;
};

}	// namespace


struct Sync::State {
	std::mutex lock;		// guards what's below up to the round lock
	std::condition_variable wake;
	bool stop = false;
	bool poked = false;
	Config config;
	std::vector<SyncedWord> mailbox;

	std::mutex round;		// one round at a time; guards the rest
	uint32_t generation = UINT32_MAX;	// of the config the rest is for
	std::string id;
	std::string name;
	std::vector<std::string> files;		// the others'
	std::map<std::string, FileStamp> read;	// as last read
	std::map<std::string, PhrasesCopy> others;	// their phrases, by file
	FileStamp userWritten;		// user.dict as ours was written from it
	FileStamp wordsWritten;		// ours, as written
	bool written = false;		// ours, since the config changed
	FileStamp phrasesStamp;		// phrases.txt as last read
	std::string phrasesText;
	uint64_t phrasesEdited = 0;
	FileStamp phrasesWrittenFrom;
	FileStamp phrasesWritten;
};


// The machine's id, made the first time: random, kept in sync-id beside
// user.dict.
static bool
MachineId(Sync::State& state, const Config& config)
{
	if (!state.id.empty())
		return true;
	std::string path = Directory(config.user) + "sync-id";
	std::string contents;
	if (!ReadAll(path, contents) || contents.size() < 16) {
		std::random_device device;
		std::mt19937_64 random((uint64_t(device()) << 32) ^ device()
			^ uint64_t(time(nullptr)));
		char id[17];
		snprintf(id, sizeof(id), "%016llx",
			static_cast<unsigned long long>(random()));
		if (!WriteAll(path, std::string(id) + "\n") || !ReadAll(path, contents))
			return false;
	}
	std::string id;
	for (char c : contents) {
		if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
			id += c;
		else
			break;
	}
	if (id.size() < 16)
		return false;
	state.id = id;
	state.name = MachineName();
	return true;
}


// A new folder, or another user.dict: everything known goes.
static void
Reset(Sync::State& state, const Config& config)
{
	if (state.generation == config.generation)
		return;
	state.generation = config.generation;
	state.id.clear();
	state.files.clear();
	state.read.clear();
	state.others.clear();
	state.userWritten = state.wordsWritten = FileStamp();
	state.written = false;
	state.phrasesStamp = state.phrasesWrittenFrom = state.phrasesWritten
		= FileStamp();
	state.phrasesText.clear();
	state.phrasesEdited = 0;
}


// The folder there, made when the folder above it is.
static bool
Reach(Sync::State& state, const Config& config, SyncReport& report)
{
	report.folder = config.folder;
	if (!MachineId(state, config)) {
		report.error = "can't keep this machine's id beside "
			+ config.user;
		return false;
	}
	struct stat info;
	if (stat(config.folder.c_str(), &info) != 0
		&& mkdir(config.folder.c_str(), 0755) != 0) {
		report.error = "can't reach " + config.folder;
		return false;
	}
	return true;
}


static bool
ParseWords(const std::string& contents, std::vector<SyncedWord>& words,
	int& lines)
{
	if (!EndsWith(contents, std::string("\n") + kEnd))
		return false;
	std::istringstream stream(contents);
	std::string line;
	while (std::getline(stream, line)) {
		if (line.empty() || line[0] == '#')
			continue;
		std::vector<std::string> fields = Fields(line);
		SyncedWord word;
		uint64_t count = 0;
		if (fields.size() == 4 && fields[0] == "-") {
			word.text = fields[1];
			word.pinyin = fields[2];
			if (!Number(fields[3], word.time))
				continue;
		} else if (fields.size() == 5) {
			word.text = fields[0];
			word.pinyin = fields[1];
			char* end = nullptr;
			word.cost = strtof(fields[4].c_str(), &end);
			if (!Number(fields[2], count) || count == 0 || count > 1000000000
				|| !Number(fields[3], word.time) || end == fields[4].c_str()) {
				continue;
			}
			word.count = uint32_t(count);
		} else
			continue;
		if (!Printable(word.text) || !Printable(word.pinyin))
			continue;
		words.push_back(std::move(word));
		lines++;
	}
	return true;
}


static bool
ParsePhrases(const std::string& contents, PhrasesCopy& copy)
{
	static const std::string kStart = "# ---\n";
	size_t start = contents.find("\n" + kStart);
	if (contents.compare(0, 2, "# ") != 0 || start == std::string::npos)
		return false;
	std::string header = contents.substr(0, start + 1);
	start += 1 + kStart.size();
	uint64_t edited = 0, bytes = 0;
	std::istringstream lines(header);
	std::string line;
	while (std::getline(lines, line)) {
		if (line.compare(0, 10, "# edited: ") == 0)
			Number(line.substr(10), edited);
		else if (line.compare(0, 9, "# bytes: ") == 0)
			Number(line.substr(9), bytes);
	}
	std::string end = std::string("\n") + kEnd;
	if (edited == 0 || contents.size() != start + bytes + end.size()
		|| contents.compare(start + bytes, end.size(), end) != 0) {
		return false;
	}
	copy.name = HeaderName(contents);
	copy.edited = edited;
	copy.text = contents.substr(start, bytes);
	return true;
}


// phrases.txt and when it was last edited. 0 for none, or for the commented
// example a new one starts as, so that never replaces anyone's phrases. What
// ours in the folder already says keeps the time it has there. Otherwise
// it's an edit here: its file's time, but once this machine takes part,
// after every edit it has seen, so an edit made after taking another
// machine's phrases wins over them even when this clock runs behind.
static void
LocalPhrases(Sync::State& state, const Config& config)
{
	FileStamp stamp = FileStamp::Of(config.phrases);
	if (stamp == state.phrasesStamp)
		return;
	state.phrasesStamp = stamp;
	state.phrasesText.clear();
	state.phrasesEdited = 0;
	if (stamp.size < 0 || !ReadAll(config.phrases, state.phrasesText)
		|| state.phrasesText == Phrases::Example()) {
		return;
	}
	state.phrasesEdited = uint64_t(std::max<int64_t>(stamp.seconds, 1));
	std::string contents;
	PhrasesCopy ours;
	if (!ReadAll(config.folder + "/" + state.id + ".phrases", contents)
		|| !ParsePhrases(contents, ours)) {
		return;
	}
	if (ours.text == state.phrasesText) {
		state.phrasesEdited = ours.edited;
		return;
	}
	uint64_t seen = ours.edited;
	for (const auto& other : state.others)
		seen = std::max(seen, other.second.edited);
	state.phrasesEdited = std::max(state.phrasesEdited, seen + 1);
}


// Another machine's phrases replace ours when they were edited later (the
// larger id when it's the same second, so every machine picks the same).
static void
TakePhrases(Sync::State& state, const Config& config, SyncReport& report)
{
	LocalPhrases(state, config);
	const PhrasesCopy* newest = nullptr;
	std::string newestId;
	for (const auto& [file, copy] : state.others) {
		std::string id = file.substr(0, file.find('.'));
		if (newest == nullptr || copy.edited > newest->edited
			|| (copy.edited == newest->edited && id > newestId)) {
			newest = &copy;
			newestId = id;
		}
	}
	if (newest == nullptr || newest->text == state.phrasesText
		|| newest->edited < state.phrasesEdited
		|| (newest->edited == state.phrasesEdited && newestId < state.id)) {
		return;
	}
	if (state.phrasesEdited > 0)
		WriteAll(config.phrases + ".bak", state.phrasesText);
	if (!WriteAll(config.phrases, newest->text))
		return;
	struct utimbuf times;
	times.actime = time(nullptr);
	times.modtime = time_t(newest->edited);
	utime(config.phrases.c_str(), &times);
	report.phrasesFrom = newest->name.empty() ? newestId : newest->name;
	// Taken, not edited here: it keeps the time it was edited.
	state.phrasesStamp = FileStamp::Of(config.phrases);
	state.phrasesText = newest->text;
	state.phrasesEdited = newest->edited;
}


static void
ReadFiles(Sync::State& state, const Config& config,
	std::vector<SyncedWord>& words, SyncReport& report, bool all)
{
	if (!Reach(state, config, report))
		return;
	// Listed every time: the folder's own modification time can't be
	// trusted, as sync tools set it (rsync -a puts back the other side's).
	state.files.clear();
	if (DIR* folder = opendir(config.folder.c_str())) {
		while (dirent* entry = readdir(folder)) {
			std::string name = entry->d_name;
			bool words = EndsWith(name, ".words");
			if (name[0] == '.' || (!words && !EndsWith(name, ".phrases"))
				|| name.substr(0, name.find('.')) == state.id) {
				continue;
			}
			state.files.push_back(name);
		}
		closedir(folder);
	}
	// Files gone: their phrases no longer count.
	for (auto copy = state.others.begin(); copy != state.others.end();) {
		bool there = false;
		for (const std::string& name : state.files)
			there = there || name == copy->first;
		copy = there ? std::next(copy) : state.others.erase(copy);
	}
	// An edit here is timed against what was seen before this round.
	LocalPhrases(state, config);

	for (const std::string& name : state.files) {
		std::string path = config.folder + "/" + name;
		FileStamp stamp = FileStamp::Of(path);
		if (!all && stamp == state.read[name])
			continue;
		state.read[name] = stamp;
		std::string contents;
		if (!ReadAll(path, contents))
			continue;
		if (EndsWith(name, ".words")) {
			int lines = 0;
			if (ParseWords(contents, words, lines)) {
				report.machines++;
				report.received += lines;
			}
		} else {
			PhrasesCopy copy;
			if (ParsePhrases(contents, copy))
				state.others[name] = copy;
		}
	}
	TakePhrases(state, config, report);
}


// user.dict's lines (count, last used, cost, text, pin yin) as ours.
static std::string
WordsFile(const std::string& name, const std::string& user, int& lines)
{
	std::string out = "# 海酷拼音 同步的词 · Haiku Pinyin synced words\n"
		"# machine: " + name + "\n"
		"# 词<Tab>拼音<Tab>次数<Tab>时间<Tab>cost · word<Tab>pinyin<Tab>count"
		"<Tab>time<Tab>cost\n"
		"# -<Tab>词<Tab>拼音<Tab>时间：删掉的词 · a forgotten word\n";
	std::istringstream stream(user);
	std::string line;
	while (std::getline(stream, line)) {
		std::vector<std::string> fields = Fields(line);
		if (fields.size() != 5 || fields[0].empty())
			continue;
		if (fields[0] == "0") {
			out += "-\t" + fields[3] + "\t" + fields[4] + "\t" + fields[1]
				+ "\n";
		} else {
			out += fields[3] + "\t" + fields[4] + "\t" + fields[0] + "\t"
				+ fields[1] + "\t" + fields[2] + "\n";
		}
		lines++;
	}
	return out + kEnd;
}


static void
WriteFiles(Sync::State& state, const Config& config, SyncReport& report,
	bool all)
{
	if (!Reach(state, config, report))
		return;
	std::string base = config.folder + "/" + state.id;

	FileStamp user = FileStamp::Of(config.user);
	std::string words = base + ".words";
	if (all || !state.written || user != state.userWritten
		|| FileStamp::Of(words) != state.wordsWritten) {
		std::string contents;
		ReadAll(config.user, contents);
		report.sent = 0;
		if (!WriteAll(words, WordsFile(state.name, contents, report.sent))) {
			report.error = "can't write " + words;
			return;
		}
		state.userWritten = user;
		state.written = true;
		state.wordsWritten = FileStamp::Of(words);
	}

	// Only phrases of our own: none yet, or the example, has nothing to say.
	LocalPhrases(state, config);
	std::string phrases = base + ".phrases";
	if (state.phrasesEdited == 0)
		return;
	if (all || state.phrasesStamp != state.phrasesWrittenFrom
		|| FileStamp::Of(phrases) != state.phrasesWritten) {
		std::string contents = "# 海酷拼音 同步的自定义短语 · Haiku Pinyin synced "
			"custom phrases\n# machine: " + state.name + "\n# edited: "
			+ std::to_string(state.phrasesEdited) + "\n# bytes: "
			+ std::to_string(state.phrasesText.size()) + "\n# ---\n"
			+ state.phrasesText + "\n" + kEnd;
		if (!WriteAll(phrases, contents)) {
			report.error = "can't write " + phrases;
			return;
		}
		state.phrasesWrittenFrom = state.phrasesStamp;
		state.phrasesWritten = FileStamp::Of(phrases);
	}
}


// The worker: a round when poked, then every few seconds for a while, then
// every minute.
static void
Work(std::shared_ptr<Sync::State> state)
{
	std::unique_lock<std::mutex> lock(state->lock);
	auto until = std::chrono::steady_clock::now() + kActive;
	while (!state->stop) {
		if (!state->poked) {
			state->wake.wait_for(lock,
				std::chrono::steady_clock::now() < until ? kEvery : kIdle);
			if (state->stop)
				break;
		}
		if (state->poked)
			until = std::chrono::steady_clock::now() + kActive;
		state->poked = false;
		Config config = state->config;
		lock.unlock();

		std::vector<SyncedWord> words;
		if (!config.folder.empty()) {
			std::lock_guard<std::mutex> round(state->round);
			Reset(*state, config);
			SyncReport report;
			ReadFiles(*state, config, words, report, false);
			WriteFiles(*state, config, report, false);
		}

		lock.lock();
		if (state->config.generation == config.generation) {
			state->mailbox.insert(state->mailbox.end(),
				std::make_move_iterator(words.begin()),
				std::make_move_iterator(words.end()));
		}
	}
}


// #pragma mark - Sync


Sync::Sync()
	:
	fState(std::make_shared<State>())
{
}


// The worker may be waiting on a slow folder: it's told to stop and left to
// finish on its own, holding its own reference to the state.
Sync::~Sync()
{
	std::lock_guard<std::mutex> lock(fState->lock);
	fState->stop = true;
	fState->wake.notify_all();
}


void
Sync::SetFolder(const std::string& folder, const std::string& userDictionary,
	const std::string& phrases)
{
	std::string place = folder.empty() ? std::string() : folder + kSubfolder;
	std::lock_guard<std::mutex> lock(fState->lock);
	if (place == fState->config.folder && userDictionary == fState->config.user
		&& phrases == fState->config.phrases) {
		return;
	}
	fFolder = folder;
	fState->config.folder = place;
	fState->config.user = userDictionary;
	fState->config.phrases = phrases;
	fState->config.generation++;
	fState->mailbox.clear();
	fState->poked = true;
	fState->wake.notify_all();
}


void
Sync::Poke()
{
	if (!On())
		return;
	if (!fStarted) {
		fStarted = true;
		std::thread(Work, fState).detach();
	}
	Wake();
}


void
Sync::Wake()
{
	if (!fStarted)
		return;
	std::lock_guard<std::mutex> lock(fState->lock);
	fState->poked = true;
	fState->wake.notify_all();
}


bool
Sync::Take(std::vector<SyncedWord>& words)
{
	std::lock_guard<std::mutex> lock(fState->lock);
	if (fState->mailbox.empty())
		return false;
	words.swap(fState->mailbox);
	fState->mailbox.clear();
	return true;
}


void
Sync::Read(std::vector<SyncedWord>& words, SyncReport& report)
{
	Config config;
	{
		std::lock_guard<std::mutex> lock(fState->lock);
		config = fState->config;
	}
	if (config.folder.empty()) {
		report.error = "no sync folder";
		return;
	}
	std::lock_guard<std::mutex> round(fState->round);
	Reset(*fState, config);
	ReadFiles(*fState, config, words, report, true);
}


void
Sync::Write(SyncReport& report)
{
	Config config;
	{
		std::lock_guard<std::mutex> lock(fState->lock);
		config = fState->config;
	}
	if (config.folder.empty()) {
		report.error = "no sync folder";
		return;
	}
	std::lock_guard<std::mutex> round(fState->round);
	Reset(*fState, config);
	WriteFiles(*fState, config, report, true);
}

}	// namespace haikupinyin
