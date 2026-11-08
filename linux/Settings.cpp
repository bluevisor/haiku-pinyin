// 海酷拼音 Haiku Pinyin for Linux: the settings, where the data is, and the
// menu in the top bar that changes them.

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "Shared.h"

#ifndef HP_DATADIR
#define HP_DATADIR "/usr/share/haiku-pinyin"
#endif


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


static std::string
ConfigDirectory()
{
	std::string path = std::string(g_get_user_config_dir()) + "/haiku-pinyin";
	g_mkdir_with_parents(path.c_str(), 0755);
	return path;
}


void
LoadSettings()
{
	std::ifstream file(ConfigDirectory() + "/settings");
	std::string line;
	haikupinyin::Options& options = sShared->savedOptions;
	while (std::getline(file, line)) {
		size_t equals = line.find('=');
		if (equals == std::string::npos)
			continue;
		std::string name = line.substr(0, equals);
		std::string value = line.substr(equals + 1);
		bool on = value == "1" || value == "true";
		if (name == "layout")
			options.layout = value;
		else if (name == "script") {
			options.script = atoi(value.c_str()) == 0
				? haikupinyin::Script::kSimplified
				: haikupinyin::Script::kTraditional;
		} else if (name == "page_size") {
			int size = atoi(value.c_str());
			if (size >= 3 && size <= 10)
				options.pageSize = size;
		} else if (name == "fullwidth")
			options.fullwidthPunctuation = on;
		else if (name == "emoji")
			options.emoji = on;
		else if (name == "english")
			options.english = on;
		else if (name == "cloud")
			options.cloud = on;
		else if (name == "fuzzy")
			options.fuzzy = unsigned(atoi(value.c_str()));
		else if (name == "vowels") {
			int vowels = atoi(value.c_str());
			if (vowels >= 0 && vowels <= 2)
				options.vowels = haikupinyin::Vowels(vowels);
		}
		else if (name == "chinese")
			sShared->savedChinese = on;
	}
}


void
SaveSettings()
{
	const haikupinyin::Options& options = sShared->engine.GetOptions();
	std::string path = ConfigDirectory() + "/settings";
	std::string staging = path + ".new";
	{
		std::ofstream file(staging);
		file << "layout=" << options.layout << "\n"
			<< "script=" << (options.script == haikupinyin::Script::kSimplified
				? 0 : 1) << "\n"
			<< "page_size=" << options.pageSize << "\n"
			<< "fullwidth=" << options.fullwidthPunctuation << "\n"
			<< "emoji=" << options.emoji << "\n"
			<< "english=" << options.english << "\n"
			<< "cloud=" << options.cloud << "\n"
			<< "fuzzy=" << options.fuzzy << "\n"
			<< "vowels=" << int(options.vowels) << "\n"
			<< "chinese=" << sShared->engine.Chinese() << "\n";
		if (!file)
			return;
	}
	rename(staging.c_str(), path.c_str());
}


static std::vector<std::string>
DataDirectories()
{
	std::vector<std::string> places;
	if (const char* data = getenv("HAIKU_PINYIN_DATA"))
		places.push_back(data);
	places.push_back(std::string(g_get_user_data_dir()) + "/haiku-pinyin");
	for (const gchar* const* dir = g_get_system_data_dirs(); *dir; dir++)
		places.push_back(std::string(*dir) + "/haiku-pinyin");
	places.push_back(HP_DATADIR);
	return places;
}


bool
EnsureLoaded()
{
	if (sShared->loaded || sShared->loadFailed)
		return sShared->loaded;

	std::string error = "no haiku-pinyin data directory";
	for (const std::string& place : DataDirectories()) {
		if (sShared->engine.Load(place, &error)) {
			sShared->loaded = true;
			break;
		}
	}
	if (!sShared->loaded) {
		g_warning("Haiku Pinyin: %s", error.c_str());
		sShared->loadFailed = true;
		return false;
	}
	sShared->engine.SetUserDictionary(ConfigDirectory() + "/user.dict");
	// Only now can the layout the settings name be checked.
	sShared->engine.SetOptions(sShared->savedOptions);
	sShared->engine.SetChinese(sShared->savedChinese);
	return true;
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
