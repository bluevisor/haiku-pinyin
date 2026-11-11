// 海酷拼音 Haiku Pinyin for Linux: the 词库 menu. The file dialogs are
// zenity's (GNOME has it) or kdialog's (KDE), the messages notify-send's;
// with neither dialog, it opens the folder and points at hpcli.

#include "Words.h"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <vector>

extern char** environ;

const WordsItem kWordsItems[4] = {
	{"words.phrases", "自定义短语…"},
	{"words.import", "导入词库…"},
	{"words.export", "导出学到的词…"},
	{"words.folder", "打开词库文件夹"},
};
const char* const kWordsLabel = "词库";

static const char* const kImportTitle
	= "导入词库：海酷拼音、Rime、搜狗，或 Mac 的文本替换";
static const char* const kExportTitle = "导出学到的词";


static bool
OnPath(const std::string& program)
{
	const char* path = getenv("PATH");
	std::string dirs = path != nullptr ? path : "/usr/bin:/bin";
	size_t start = 0;
	while (start <= dirs.size()) {
		size_t colon = dirs.find(':', start);
		if (colon == std::string::npos)
			colon = dirs.size();
		std::string file = dirs.substr(start, colon - start) + "/" + program;
		if (colon > start && access(file.c_str(), X_OK) == 0)
			return true;
		start = colon + 1;
	}
	return false;
}


// Starts a program from PATH, its output to out's read end when out is
// given, to /dev/null otherwise. -1 when it can't.
static pid_t
Spawn(const std::vector<std::string>& args, int* out = nullptr)
{
	std::vector<char*> argv;
	for (const std::string& arg : args)
		argv.push_back(const_cast<char*>(arg.c_str()));
	argv.push_back(nullptr);
	int ends[2] = {-1, -1};
	if (out != nullptr && pipe2(ends, O_CLOEXEC) != 0)
		return -1;
	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	if (out != nullptr)
		posix_spawn_file_actions_adddup2(&actions, ends[1], 1);
	else
		posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
	posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
	pid_t pid;
	bool started = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(),
		environ) == 0;
	posix_spawn_file_actions_destroy(&actions);
	if (out != nullptr) {
		close(ends[1]);
		if (started)
			*out = ends[0];
		else
			close(ends[0]);
	}
	return started ? pid : -1;
}


// Starts it and lets it be, reaped on a thread of its own.
static bool
Run(const std::vector<std::string>& args)
{
	pid_t pid = Spawn(args);
	if (pid < 0)
		return false;
	std::thread([pid]() { waitpid(pid, nullptr, 0); }).detach();
	return true;
}


static void
Open(const std::string& path)
{
	if (!Run({"xdg-open", path}))
		fprintf(stderr, "Haiku Pinyin: can't open %s\n", path.c_str());
}


// A desktop notification, or a dialog, or the log.
static void
Tell(const std::string& message)
{
	if (OnPath("notify-send")
		&& Run({"notify-send", "-a", "海酷拼音", "海酷拼音", message})) {
		return;
	}
	if (OnPath("zenity") && Run({"zenity", "--info", "--title=海酷拼音",
			"--no-markup", "--text=" + message})) {
		return;
	}
	if (OnPath("kdialog")
		&& Run({"kdialog", "--title", "海酷拼音", "--msgbox", message})) {
		return;
	}
	fprintf(stderr, "Haiku Pinyin: %s\n", message.c_str());
}


static std::string
Folder(const haikupinyin::Engine& engine)
{
	const std::string& phrases = engine.PhrasesFile();
	return phrases.substr(0, phrases.rfind('/'));
}


static void
Import(haikupinyin::Engine& engine, const std::string& path)
{
	Tell(engine.ImportWords(path).Summary());
}


static void
Export(haikupinyin::Engine& engine, const std::string& path)
{
	int count = 0;
	if (engine.ExportWords(path, &count))
		Tell("导出了 " + std::to_string(count) + " 个学到的词：" + path);
	else
		Tell("没能写入 " + path);
}


WordsMenu::WordsMenu(Post post, Ready ready)
	:
	fPost(post),
	fReady(ready)
{
}


WordsMenu::~WordsMenu()
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		if (fDialog > 0)
			kill(fDialog, SIGTERM);
	}
	if (fThread.joinable())
		fThread.join();
}


bool
WordsMenu::Activate(const std::string& key)
{
	bool known = false;
	for (const WordsItem& item : kWordsItems)
		known = known || key == item.key;
	if (!known)
		return false;
	haikupinyin::Engine* engine = fReady();
	if (engine == nullptr)
		return true;
	if (key == "words.phrases") {
		engine->CustomPhrases().CreateFile();
		Open(engine->PhrasesFile());
	} else if (key == "words.folder")
		Open(Folder(*engine));
	else if (!_Choose(key == "words.export")) {
		// No dialog to ask with: the folder, and hpcli.
		Open(Folder(*engine));
		Tell("没有 zenity 或 kdialog，选不了文件。可以用 hpcli import / export。");
	}
	return true;
}


// The file dialog, waited for on a thread; what it chose is imported or
// exported on the main loop. False when there's no dialog to show.
bool
WordsMenu::_Choose(bool save)
{
	std::lock_guard<std::mutex> lock(fLock);
	if (fDialog > 0)
		return true;	// one is open already
	if (fThread.joinable())
		fThread.join();	// the last one, done

	const char* home = getenv("HOME");
	std::string name = std::string(home != nullptr ? home : "") + "/海酷拼音词库.txt";
	std::vector<std::string> args;
	if (OnPath("zenity")) {
		args = {"zenity", "--file-selection"};
		if (save) {
			args.insert(args.end(), {"--save", "--confirm-overwrite",
				"--filename=" + name, std::string("--title=") + kExportTitle});
		} else
			args.push_back(std::string("--title=") + kImportTitle);
	} else if (OnPath("kdialog")) {
		args = {"kdialog", "--title", save ? kExportTitle : kImportTitle};
		if (save)
			args.insert(args.end(), {"--getsavefilename", name});
		else
			args.insert(args.end(), {"--getopenfilename", home != nullptr ? home : "/"});
	} else
		return false;

	int out = -1;
	pid_t pid = Spawn(args, &out);
	if (pid < 0)
		return false;
	fDialog = pid;
	fThread = std::thread([this, pid, out, save]() {
		std::string path;
		char buffer[4096];
		ssize_t got;
		while ((got = read(out, buffer, sizeof(buffer))) != 0) {
			if (got > 0)
				path.append(buffer, size_t(got));
			else if (errno != EINTR)
				break;
		}
		close(out);
		int status = 0;
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
			;
		{
			std::lock_guard<std::mutex> lock(fLock);
			fDialog = -1;
		}
		while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
			path.pop_back();
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || path.empty())
			return;	// cancelled
		fPost([this, path, save]() {
			if (haikupinyin::Engine* engine = fReady()) {
				if (save)
					Export(*engine, path);
				else
					Import(*engine, path);
			}
		});
	});
	return true;
}
