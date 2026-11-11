// Haiku Pinyin: the learned words and the custom phrases kept alike on
// several machines, through a folder the user's own tool already syncs
// (iCloud Drive, Dropbox, Syncthing, Nextcloud, a network share). There's no
// server.
//
// In <folder>/haiku-pinyin/ each machine writes only its own two files,
// <id>.words and <id>.phrases, and reads the others'. No two machines ever
// change one file, so a sync tool has nothing to make conflict copies of.
//
// <id>.words holds every word this machine knows, learned here or taken
// from the others, as text: 词<Tab>pin yin<Tab>count<Tab>time<Tab>cost, and
// -<Tab>词<Tab>pin yin<Tab>time for a word forgotten (Ctrl+Delete). Reading
// one joins it into user.dict (UserDict::Join): the higher count, never the
// sum, so the same word learned on two machines isn't counted again each
// round; a word forgotten at or after it was last learned stays forgotten
// everywhere, and learning it again anywhere afterward brings it back.
//
// <id>.phrases is this machine's phrases.txt as it stands, with the time it
// was last edited. The newest edit wins as a whole file: it replaces
// phrases.txt on the others, which keep the one it replaced as
// phrases.txt.bak. An edit made after taking another machine's phrases
// counts as newer than them even when this machine's clock runs behind.
//
// Files are written to a hidden temporary name, then renamed into place. A
// file counts only once it's whole (it ends with "# end"); lines in it that
// don't parse are skipped.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace haikupinyin {

// A line of another machine's words file, not yet looked up in the lexicon.
struct SyncedWord {
	std::string text;
	std::string pinyin;		// syllables, spaces between
	uint32_t count = 0;		// 0: forgotten
	uint64_t time = 0;		// UserDict::Time()
	float cost = 0;
};

// What a round did, for hpcli sync.
struct SyncReport {
	std::string error;		// why it couldn't: "" when it did
	std::string folder;		// <folder>/haiku-pinyin
	int sent = 0;			// words and forgotten words in ours
	int machines = 0;		// the others' words files read
	int received = 0;		// the lines in them
	int changed = 0;		// words that changed here
	std::string phrasesFrom;	// the machine whose phrases replaced ours
};

// The setting: the folder, in a file named sync-folder beside user.dict, so
// every front end and hpcli share it, and a running engine takes a change up
// with a stat. Empty or missing: off.
std::string SyncSettingFile(const std::string& userDictionary);
std::string ReadSyncSetting(const std::string& userDictionary);
bool WriteSyncSetting(const std::string& userDictionary,
	const std::string& folder);


class Sync {
public:
	Sync();
	~Sync();
	Sync(const Sync&) = delete;
	Sync& operator=(const Sync&) = delete;

	// "" turns it off. The machine's id is kept in sync-id beside
	// userDictionary.
	void SetFolder(const std::string& folder, const std::string& userDictionary,
		const std::string& phrases);
	const std::string& Folder() const { return fFolder; }
	bool On() const { return !fFolder.empty(); }

	// For a running input method: a worker thread (started the first time)
	// looks now, and every few seconds for a while after. Never waits on the
	// disk.
	void Poke();
	// The dictionary was saved: the worker, if it runs, writes ours.
	void Wake();
	// What the others' files brought since the last call.
	bool Take(std::vector<SyncedWord>& words);

	// The two halves of a round, on this thread (hpcli sync, tests): every
	// other machine's file in (taking their phrases when they're newer),
	// then ours out.
	void Read(std::vector<SyncedWord>& words, SyncReport& report);
	void Write(SyncReport& report);

	struct State;

private:
	std::shared_ptr<State> fState;
	std::string fFolder;
	bool fStarted = false;
};

}	// namespace haikupinyin
