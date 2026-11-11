// 海酷拼音 Haiku Pinyin for Linux: the 词库 menu, for both front ends (the
// IBus top-bar menu, Fcitx5's status area): the custom phrases file, and
// importing and exporting learned words through a file dialog.
#pragma once

#include <sys/types.h>

#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "../engine/Engine.h"

// The menu's items, in order: key and label.
struct WordsItem {
	const char* key;
	const char* label;
};
extern const WordsItem kWordsItems[4];
extern const char* const kWordsLabel;	// the menu's own label


class WordsMenu {
public:
	// Runs a function on the front end's main loop; called from another
	// thread.
	typedef std::function<void(std::function<void()>)> Post;
	// The engine, loaded, with nothing being typed; nullptr when it can't
	// be. Called on the main loop.
	typedef std::function<haikupinyin::Engine*()> Ready;

	WordsMenu(Post post, Ready ready);
	~WordsMenu();	// closes a dialog still open

	// One of kWordsItems' keys; false for anything else.
	bool Activate(const std::string& key);

private:
	bool _Choose(bool save);

	Post fPost;
	Ready fReady;
	std::mutex fLock;
	pid_t fDialog = -1;
	std::thread fThread;
};
