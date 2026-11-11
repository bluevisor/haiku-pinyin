// 海酷拼音 Haiku Pinyin for Linux: the settings, where the data is, and the
// menu in the top bar that changes them. The settings window
// (setup/Setup.cpp) changes the file, which the engine watches.

#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

#include "Shared.h"
#include "Words.h"


static const struct {
	const char* layout;
	const char* label;
} kLayouts[] = {
	{"full", "全拼"},
	{"pyjj", "双拼 · 拼音加加"},
	{"xiaohe", "双拼 · 小鹤"},
	{"ziranma", "双拼 · 自然码"},
	{"mspy", "双拼 · 微软"},
	{"abc", "双拼 · 智能ABC"},
};

static const char* const kScripts[] = { "简体中文", "繁體中文" };

// How shuangpin spells a, o and e; in the order of haikupinyin::Vowels.
static const char* const kVowels[] = {
	"双拼 a o e: aa oo ee", "双拼 a o e: oa oo oe", "双拼 a o e: a o e 单键",
};

static const struct {
	unsigned mask;
	const char* label;
} kFuzzyGroups[] = {
	{haikupinyin::kFuzzyRetroflex, "模糊音  z=zh  c=ch  s=sh"},
	{haikupinyin::kFuzzyNasal, "模糊音  an=ang  en=eng  in=ing"},
	{haikupinyin::kFuzzyNL | haikupinyin::kFuzzyFH | haikupinyin::kFuzzyRL,
		"模糊音  n=l  f=h  r=l"},
};


void
LoadSettings()
{
	ReadSettings(&sShared->savedOptions, &sShared->savedChinese);
}


void
SaveSettings()
{
	WriteSettings(sShared->engine.GetOptions(), sShared->engine.Chinese());
}


// The file changed. Its own saves come back here too, and change nothing.
static void
SettingsChanged(GFileMonitor*, GFile*, GFile*, GFileMonitorEvent event,
	gpointer)
{
	if (event == G_FILE_MONITOR_EVENT_DELETED)
		return;
	haikupinyin::Options options;
	bool chinese = true;
	ReadSettings(&options, &chinese);
	if (!sShared->loaded) {
		sShared->savedOptions = options;
		sShared->savedChinese = chinese;
		return;
	}
	haikupinyin::Engine& engine = sShared->engine;
	if (SameSettings(options, engine.GetOptions()) && chinese == engine.Chinese())
		return;
	IBusEngine* shown = sShared->focused != nullptr ? sShared->focused
		: sShared->last;
	if (sShared->last != nullptr)
		Cancel(sShared->last);
	else
		engine.Reset();
	engine.SetOptions(options);
	engine.SetChinese(chinese);
	if (shown != nullptr)
		UpdateProperties(shown, true);
}


void
WatchSettings()
{
	GFile* file = g_file_new_for_path((ConfigDirectory() + "/settings").c_str());
	// Kept for the life of the engine.
	GFileMonitor* monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE,
		nullptr, nullptr);
	g_object_unref(file);
	if (monitor != nullptr) {
		g_signal_connect(monitor, "changed", G_CALLBACK(SettingsChanged),
			nullptr);
	}
}


std::string
SetupProgram()
{
	char self[4096];
	ssize_t length = readlink("/proc/self/exe", self, sizeof(self) - 1);
	if (length <= 0)
		return std::string();
	std::string path(self, size_t(length));
	path = path.substr(0, path.rfind('/') + 1) + "haiku-pinyin-setup";
	return access(path.c_str(), X_OK) == 0 ? path : std::string();
}


static void
OpenSetup()
{
	std::string program = SetupProgram();
	if (program.empty())
		return;
	const gchar* argv[] = {program.c_str(), nullptr};
	GError* error = nullptr;
	if (!g_spawn_async(nullptr, const_cast<gchar**>(argv), nullptr,
			G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, &error)) {
		g_warning("Haiku Pinyin: %s", error->message);
		g_clear_error(&error);
	}
}


bool
EnsureLoaded()
{
	if (sShared->loaded || sShared->loadFailed)
		return sShared->loaded;

	std::string error;
	if (!LoadData(&sShared->engine, &error)) {
		g_warning("Haiku Pinyin: %s", error.c_str());
		sShared->loadFailed = true;
		return false;
	}
	sShared->loaded = true;
	// Only now can the layout the settings name be checked.
	sShared->engine.SetOptions(sShared->savedOptions);
	sShared->engine.SetChinese(sShared->savedChinese);
	return true;
}


// The 词库 menu's work, done on the main loop.
static WordsMenu&
Words()
{
	static WordsMenu sWords([](std::function<void()> work) {
		g_idle_add([](gpointer data) {
			auto* work = static_cast<std::function<void()>*>(data);
			(*work)();
			delete work;
			return G_SOURCE_REMOVE;
		}, new std::function<void()>(std::move(work)));
	}, []() -> haikupinyin::Engine* {
		if (!EnsureLoaded())
			return nullptr;
		if (sShared->last != nullptr)
			Cancel(sShared->last);
		return &sShared->engine;
	});
	return sWords;
}


static IBusProperty*
NewProperty(const char* key, IBusPropType type, const char* label,
	bool checked = false, IBusPropList* children = nullptr)
{
	return ibus_property_new(key, type, ibus_text_new_from_string(label),
		nullptr, nullptr, TRUE, TRUE,
		checked ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED, children);
}


static IBusPropList*
BuildProperties()
{
	bool chinese = sShared->loaded ? sShared->engine.Chinese()
		: sShared->savedChinese;
	const haikupinyin::Options& options = sShared->loaded
		? sShared->engine.GetOptions() : sShared->savedOptions;

	IBusPropList* list = ibus_prop_list_new();

	// GNOME Shell shows the "InputMode" property's symbol in the top bar.
	IBusProperty* mode = NewProperty("InputMode", PROP_TYPE_NORMAL,
		chinese ? "中文 (Shift 切换)" : "English (Shift 切换)");
	ibus_property_set_symbol(mode,
		ibus_text_new_from_string(chinese ? "中" : "英"));
	ibus_property_set_tooltip(mode,
		ibus_text_new_from_string("中 / 英 (Shift)"));
	ibus_prop_list_append(list, mode);

	IBusPropList* layouts = ibus_prop_list_new();
	std::string layoutLabel = "全拼";
	for (const auto& layout : kLayouts) {
		bool marked = options.layout == layout.layout;
		if (marked)
			layoutLabel = layout.label;
		ibus_prop_list_append(layouts, NewProperty(
			(std::string("layout.") + layout.layout).c_str(), PROP_TYPE_RADIO,
			layout.label, marked));
	}
	ibus_prop_list_append(list, NewProperty("layout", PROP_TYPE_MENU,
		layoutLabel.c_str(), false, layouts));

	// Always there: GNOME Shell takes the menu's shape only once.
	IBusPropList* vowels = ibus_prop_list_new();
	int chosen = int(options.vowels);
	for (int i = 0; i < 3; i++) {
		ibus_prop_list_append(vowels, NewProperty(
			(std::string("vowels.") + std::to_string(i)).c_str(),
			PROP_TYPE_RADIO, kVowels[i], chosen == i));
	}
	ibus_prop_list_append(list, NewProperty("vowels", PROP_TYPE_MENU,
		kVowels[chosen], false, vowels));

	IBusPropList* scripts = ibus_prop_list_new();
	int script = options.script == haikupinyin::Script::kSimplified ? 0 : 1;
	for (int i = 0; i < 2; i++) {
		ibus_prop_list_append(scripts, NewProperty(
			(std::string("script.") + std::to_string(i)).c_str(),
			PROP_TYPE_RADIO, kScripts[i], script == i));
	}
	ibus_prop_list_append(list, NewProperty("script", PROP_TYPE_MENU,
		kScripts[script], false, scripts));

	IBusPropList* toggles = ibus_prop_list_new();
	ibus_prop_list_append(toggles, NewProperty("fullwidth", PROP_TYPE_TOGGLE,
		"全角标点", options.fullwidthPunctuation));
	ibus_prop_list_append(toggles, NewProperty("emoji", PROP_TYPE_TOGGLE,
		"Emoji 候选", options.emoji));
	ibus_prop_list_append(toggles, NewProperty("english", PROP_TYPE_TOGGLE,
		"中英混输", options.english));
	ibus_prop_list_append(toggles, NewProperty("cloud", PROP_TYPE_TOGGLE,
		"云候选 (Google)", options.cloud));
	for (const auto& group : kFuzzyGroups) {
		ibus_prop_list_append(toggles, NewProperty(
			(std::string("fuzzy.") + std::to_string(group.mask)).c_str(),
			PROP_TYPE_TOGGLE, group.label,
			(options.fuzzy & group.mask) == group.mask));
	}
	ibus_prop_list_append(list, NewProperty("options", PROP_TYPE_MENU,
		"选项", false, toggles));

	IBusPropList* sizes = ibus_prop_list_new();
	for (int size : {5, 7, 9}) {
		std::string label = "每页 " + std::to_string(size) + " 个候选";
		ibus_prop_list_append(sizes, NewProperty(
			(std::string("page.") + std::to_string(size)).c_str(),
			PROP_TYPE_RADIO, label.c_str(), options.pageSize == size));
	}
	ibus_prop_list_append(list, NewProperty("page", PROP_TYPE_MENU,
		("每页 " + std::to_string(options.pageSize) + " 个候选").c_str(),
		false, sizes));

	IBusPropList* words = ibus_prop_list_new();
	for (const WordsItem& item : kWordsItems) {
		ibus_prop_list_append(words, NewProperty(item.key, PROP_TYPE_NORMAL,
			item.label));
	}
	ibus_prop_list_append(list, NewProperty("words", PROP_TYPE_MENU,
		kWordsLabel, false, words));

	// The settings window, when it's installed.
	static const bool sSetup = !SetupProgram().empty();
	if (sSetup)
		ibus_prop_list_append(list, NewProperty("setup", PROP_TYPE_NORMAL, "设置…"));
	return list;
}


static void
SendUpdates(IBusEngine* engine, IBusPropList* list)
{
	IBusProperty* property;
	for (guint i = 0; (property = ibus_prop_list_get(list, i)) != nullptr; i++) {
		ibus_engine_update_property(engine, property);
		if (ibus_property_get_prop_type(property) == PROP_TYPE_MENU)
			SendUpdates(engine, ibus_property_get_sub_props(property));
	}
}


// Registering hands the panel the whole menu, but GNOME Shell only takes
// that once, as the engine is switched to; after that it follows
// update-property alone, one item at a time.
void
UpdateProperties(IBusEngine* engine, bool changed)
{
	IBusPropList* list = BuildProperties();
	g_object_ref_sink(list);
	ibus_engine_register_properties(engine, list);
	if (changed)
		SendUpdates(engine, list);
	g_object_unref(list);
}


void
PropertyActivate(IBusEngine* engine, const gchar* name, guint state)
{
	if (!EnsureLoaded())
		return;
	std::string property = name;
	haikupinyin::Options options = sShared->engine.GetOptions();
	auto starts = [&](const char* prefix, std::string* rest) {
		size_t length = strlen(prefix);
		if (property.compare(0, length, prefix) != 0)
			return false;
		*rest = property.substr(length);
		return true;
	};
	std::string rest;

	if (property == "InputMode") {
		ToggleChinese(engine);
		return;
	}
	if (property == "setup") {
		OpenSetup();
		return;
	}
	if (Words().Activate(property))
		return;
	// Radio items are also activated as they're unchecked.
	bool radio = property.rfind("layout.", 0) == 0
		|| property.rfind("script.", 0) == 0 || property.rfind("page.", 0) == 0
		|| property.rfind("vowels.", 0) == 0;
	if (radio && state != PROP_STATE_CHECKED)
		return;

	if (starts("layout.", &rest))
		options.layout = rest;
	else if (starts("script.", &rest)) {
		options.script = rest == "0" ? haikupinyin::Script::kSimplified
			: haikupinyin::Script::kTraditional;
	} else if (starts("page.", &rest))
		options.pageSize = atoi(rest.c_str());
	else if (starts("vowels.", &rest))
		options.vowels = haikupinyin::Vowels(atoi(rest.c_str()));
	else if (property == "fullwidth")
		options.fullwidthPunctuation = !options.fullwidthPunctuation;
	else if (property == "emoji")
		options.emoji = !options.emoji;
	else if (property == "english")
		options.english = !options.english;
	else if (property == "cloud")
		options.cloud = !options.cloud;
	else if (starts("fuzzy.", &rest))
		options.fuzzy ^= unsigned(atoi(rest.c_str()));
	else
		return;

	if (sShared->last == engine)
		Cancel(engine);
	else
		sShared->engine.Reset();
	sShared->engine.SetOptions(options);
	UpdateProperties(engine, true);
	SaveSettings();
}
