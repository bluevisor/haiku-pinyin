// 海酷拼音 Haiku Pinyin for Linux: the 词库 menu in the top bar: the custom
// phrases file, and importing and exporting learned words through zenity's
// file dialogs (GNOME has it), or the folder when there's no zenity.

#include <gio/gio.h>

#include <string>
#include <vector>

#include "Shared.h"


static std::string
Folder()
{
	const std::string& phrases = sShared->engine.PhrasesFile();
	return phrases.substr(0, phrases.rfind('/'));
}


static void
Open(const std::string& path)
{
	gchar* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
	GError* error = nullptr;
	if (uri == nullptr
		|| !g_app_info_launch_default_for_uri(uri, nullptr, &error)) {
		g_warning("Haiku Pinyin: can't open %s: %s", path.c_str(),
			error != nullptr ? error->message : "");
	}
	g_clear_error(&error);
	g_free(uri);
}


// A desktop notification, or a dialog, or the log.
static void
Tell(const std::string& message)
{
	gchar* notify = g_find_program_in_path("notify-send");
	gchar* zenity = g_find_program_in_path("zenity");
	std::string text = "--text=" + message;
	std::vector<const gchar*> argv;
	if (notify != nullptr)
		argv = {notify, "-a", "海酷拼音", "海酷拼音", message.c_str(), nullptr};
	else if (zenity != nullptr) {
		argv = {zenity, "--info", "--title=海酷拼音", "--no-markup",
			text.c_str(), nullptr};
	}
	if (argv.empty() || !g_spawn_async(nullptr, const_cast<gchar**>(argv.data()),
			nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, nullptr)) {
		g_message("Haiku Pinyin: %s", message.c_str());
	}
	g_free(notify);
	g_free(zenity);
}


typedef void (*Chosen)(const std::string& path);


static void
Answered(GObject* source, GAsyncResult* result, gpointer data)
{
	GSubprocess* process = G_SUBPROCESS(source);
	gchar* out = nullptr;
	if (g_subprocess_communicate_utf8_finish(process, result, &out, nullptr,
			nullptr)
		&& g_subprocess_get_successful(process) && out != nullptr) {
		std::string path = out;
		while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
			path.pop_back();
		if (!path.empty())
			reinterpret_cast<Chosen>(data)(path);
	}
	g_free(out);
	g_object_unref(process);
}


// zenity's file dialog, on the side; done gets the file chosen. False when
// there's no zenity.
static bool
Choose(bool save, Chosen done)
{
	gchar* zenity = g_find_program_in_path("zenity");
	if (zenity == nullptr)
		return false;
	std::string name = std::string("--filename=") + g_get_home_dir()
		+ "/海酷拼音词库.txt";
	std::vector<const gchar*> argv = {zenity, "--file-selection"};
	if (save) {
		argv.insert(argv.end(), {"--save", "--confirm-overwrite",
			name.c_str(), "--title=导出学到的词"});
	} else
		argv.push_back("--title=导入词库：海酷拼音、Rime、搜狗，或 Mac 的文本替换");
	argv.push_back(nullptr);
	GError* error = nullptr;
	GSubprocess* process = g_subprocess_newv(argv.data(),
		GSubprocessFlags(G_SUBPROCESS_FLAGS_STDOUT_PIPE
			| G_SUBPROCESS_FLAGS_STDERR_SILENCE), &error);
	g_free(zenity);
	if (process == nullptr) {
		g_clear_error(&error);
		return false;
	}
	g_subprocess_communicate_utf8_async(process, nullptr, nullptr, Answered,
		reinterpret_cast<gpointer>(done));
	return true;
}


static void
Import(const std::string& path)
{
	if (!EnsureLoaded())
		return;
	if (sShared->last != nullptr)
		Cancel(sShared->last);
	Tell(sShared->engine.ImportWords(path).Summary());
}


static void
Export(const std::string& path)
{
	if (!EnsureLoaded())
		return;
	int count = 0;
	if (sShared->engine.ExportWords(path, &count))
		Tell("导出了 " + std::to_string(count) + " 个学到的词：" + path);
	else
		Tell("没能写入 " + path);
}


static IBusProperty*
Item(const char* key, const char* label)
{
	return ibus_property_new(key, PROP_TYPE_NORMAL,
		ibus_text_new_from_string(label), nullptr, nullptr, TRUE, TRUE,
		PROP_STATE_UNCHECKED, nullptr);
}


void
AppendWordsMenu(IBusPropList* list)
{
	IBusPropList* items = ibus_prop_list_new();
	ibus_prop_list_append(items, Item("words.phrases", "自定义短语…"));
	ibus_prop_list_append(items, Item("words.import", "导入词库…"));
	ibus_prop_list_append(items, Item("words.export", "导出学到的词…"));
	ibus_prop_list_append(items, Item("words.folder", "打开词库文件夹"));
	ibus_prop_list_append(list, ibus_property_new("words", PROP_TYPE_MENU,
		ibus_text_new_from_string("词库"), nullptr, nullptr, TRUE, TRUE,
		PROP_STATE_UNCHECKED, items));
}


bool
WordsActivate(const std::string& property)
{
	if (property.rfind("words.", 0) != 0)
		return false;
	if (property == "words.phrases") {
		sShared->engine.CustomPhrases().CreateFile();
		Open(sShared->engine.PhrasesFile());
	} else if (property == "words.folder")
		Open(Folder());
	else if (!Choose(property == "words.export",
			property == "words.export" ? Export : Import)) {
		// No dialog to ask with: the folder, and hpcli.
		Open(Folder());
		Tell("没有 zenity，选不了文件。可以用 hpcli import / export。");
	}
	return true;
}
