// 海酷拼音 Haiku Pinyin for Linux: an IBus engine around the portable engine.
//   ibus-engine-haiku-pinyin --ibus     started by ibus-daemon (component XML)
//   ibus-engine-haiku-pinyin            registers itself, for development:
//                                       then `ibus engine haiku-pinyin`
//
// IBus makes one engine object per input context; they share one
// haikupinyin::Engine (the dictionaries are large), which is reset whenever
// focus moves to another context.

#include <ibus.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

#include "../engine/Engine.h"

#ifndef HP_DATADIR
#define HP_DATADIR "/usr/share/haiku-pinyin"
#endif
#ifndef HP_VERSION
#define HP_VERSION "0"
#endif

using haikupinyin::Key;

static const char* const kComponentName = "org.freedesktop.IBus.HaikuPinyin";
static const char* const kEngineName = "haiku-pinyin";
static const gint64 kShiftTapLimit = 400000;	// µs
static const int kCloudDelay = 120;				// ms, let typing settle

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


// #pragma mark - shared state


struct Shared {
	haikupinyin::Engine engine;
	haikupinyin::Options savedOptions;	// applied once the data loads
	bool savedChinese = true;
	bool loaded = false;
	bool loadFailed = false;
	IBusEngine* focused = nullptr;
	IBusEngine* last = nullptr;			// the context the engine state is for
	std::string cloudAsked;				// in flight
	std::string cloudAnswered;			// last asked
};

static Shared* sShared;


static std::string
ConfigDirectory()
{
	std::string path = std::string(g_get_user_config_dir()) + "/haiku-pinyin";
	g_mkdir_with_parents(path.c_str(), 0755);
	return path;
}


static void
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


static void
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


static bool
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


// #pragma mark - the IBus engine


struct HaikuPinyinEngine {
	IBusEngine parent;
	std::set<guint>* swallowed;		// keycodes whose releases we eat
	bool shiftAlone;
	gint64 shiftDown;
	bool preeditShown;
};

struct HaikuPinyinEngineClass {
	IBusEngineClass parent;
};

G_DEFINE_TYPE(HaikuPinyinEngine, haiku_pinyin_engine, IBUS_TYPE_ENGINE)

#define HP_ENGINE(object) (reinterpret_cast<HaikuPinyinEngine*>(object))


static void AskCloud();


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
static void
UpdateProperties(IBusEngine* engine, bool changed = false)
{
	IBusPropList* list = BuildProperties();
	g_object_ref_sink(list);
	ibus_engine_register_properties(engine, list);
	if (changed)
		SendUpdates(engine, list);
	g_object_unref(list);
}


static void
HidePreedit(IBusEngine* engine)
{
	HaikuPinyinEngine* self = HP_ENGINE(engine);
	if (!self->preeditShown)
		return;
	ibus_engine_hide_preedit_text(engine);
	ibus_engine_update_preedit_text(engine, ibus_text_new_from_string(""), 0,
		FALSE);
	self->preeditShown = false;
}


// Empties the panel's table and label as well as hiding them, so a stale
// one can't come back with the next show.
static void
HideCandidates(IBusEngine* engine)
{
	IBusLookupTable* empty = ibus_lookup_table_new(1, 0, FALSE, FALSE);
	ibus_engine_update_lookup_table(engine, empty, FALSE);
	ibus_engine_update_auxiliary_text(engine, ibus_text_new_from_string(""),
		FALSE);
	ibus_engine_hide_lookup_table(engine);
	ibus_engine_hide_auxiliary_text(engine);
}


static void
ShowCandidates(IBusEngine* engine)
{
	haikupinyin::Engine& pinyin = sShared->engine;
	if (!pinyin.Composing()) {
		HideCandidates(engine);
		return;
	}
	AskCloud();

	int pageSize = pinyin.GetOptions().pageSize;
	std::vector<std::string> page = pinyin.PageCandidates();
	int highlight = pinyin.Highlighted();
	int cloud = pinyin.CloudIndexOnPage();

	// The engine pages by itself; the panel only sees this page. Blank
	// entries stand in for the pages around it so the panel draws its
	// arrows and its page_up/page_down come back to us.
	IBusLookupTable* table = ibus_lookup_table_new(pageSize, 0,
		highlight >= 0, FALSE);
	int before = pinyin.Page() * pageSize;
	for (int i = 0; i < before; i++)
		ibus_lookup_table_append_candidate(table, ibus_text_new_from_string(""));
	for (size_t i = 0; i < page.size(); i++) {
		std::string text = page[i];
		if (int(i) == cloud)
			text += " ☁";
		ibus_lookup_table_append_candidate(table,
			ibus_text_new_from_string(text.c_str()));
	}
	if (pinyin.HasNextPage()) {
		for (int i = int(page.size()); i < 2 * pageSize; i++) {
			ibus_lookup_table_append_candidate(table,
				ibus_text_new_from_string(""));
		}
	}
	for (int i = 0; i < pageSize; i++) {
		ibus_lookup_table_set_label(table, i,
			ibus_text_new_from_string((std::to_string(i + 1) + ".").c_str()));
	}
	ibus_lookup_table_set_cursor_pos(table,
		guint(before + (highlight >= 0 ? highlight : 0)));
	ibus_lookup_table_set_cursor_visible(table, highlight >= 0);
	ibus_lookup_table_set_orientation(table, IBUS_ORIENTATION_HORIZONTAL);
	ibus_engine_update_lookup_table(engine, table, TRUE);

	// The spelled pinyin while composing.
	if (pinyin.Composing()) {
		ibus_engine_update_auxiliary_text(engine,
			ibus_text_new_from_string(pinyin.Spelled().c_str()), TRUE);
	} else {
		ibus_engine_update_auxiliary_text(engine,
			ibus_text_new_from_string(""), FALSE);
	}
}


// Turns the engine's state into IBus calls: what to commit, the inline
// composition, the candidates.
static void
Sync(IBusEngine* engine)
{
	haikupinyin::Engine& pinyin = sShared->engine;
	HaikuPinyinEngine* self = HP_ENGINE(engine);

	std::string commit = pinyin.TakeCommit();
	if (!commit.empty()) {
		HidePreedit(engine);
		ibus_engine_commit_text(engine,
			ibus_text_new_from_string(commit.c_str()));
	}

	if (pinyin.Composing()) {
		std::string preedit = pinyin.Preedit();
		size_t picked = pinyin.PickedLength();
		IBusText* text = ibus_text_new_from_string(preedit.c_str());
		guint length = guint(g_utf8_strlen(preedit.c_str(), -1));
		guint pickedChars = guint(g_utf8_strlen(preedit.c_str(),
			gssize(picked)));
		// The picked text and the pinyin still being typed: two clauses.
		ibus_text_append_attribute(text, IBUS_ATTR_TYPE_UNDERLINE,
			IBUS_ATTR_UNDERLINE_SINGLE, 0, length);
		if (pickedChars > 0) {
			ibus_text_append_attribute(text, IBUS_ATTR_TYPE_UNDERLINE,
				IBUS_ATTR_UNDERLINE_DOUBLE, 0, pickedChars);
		}
		ibus_engine_update_preedit_text_with_mode(engine, text, length, TRUE,
			IBUS_ENGINE_PREEDIT_CLEAR);
		self->preeditShown = true;
	} else
		HidePreedit(engine);

	ShowCandidates(engine);
}


static void
Cancel(IBusEngine* engine)
{
	sShared->engine.Reset();
	Sync(engine);
}


// The shared engine now serves this context.
static void
Claim(IBusEngine* engine)
{
	sShared->focused = engine;
	if (sShared->last != engine) {
		if (sShared->last != nullptr) {
			HidePreedit(sShared->last);
			HideCandidates(sShared->last);
		}
		if (sShared->loaded) {
			sShared->engine.Reset();
			sShared->engine.ClearContext();
		}
		sShared->last = engine;
	}
}


static bool
MapKey(guint keyval, guint state, Key* key, char32_t* character,
	uint32_t* modifiers)
{
	*modifiers = 0;
	if (state & IBUS_SHIFT_MASK)
		*modifiers |= haikupinyin::kShift;
	if (state & IBUS_CONTROL_MASK)
		*modifiers |= haikupinyin::kControl;
	if (state & (IBUS_SUPER_MASK | IBUS_MOD4_MASK | IBUS_META_MASK))
		*modifiers |= haikupinyin::kCommand;
	if (state & IBUS_MOD1_MASK)
		*modifiers |= haikupinyin::kOption;

	*character = 0;
	switch (keyval) {
		case IBUS_KEY_BackSpace: *key = Key::kBackspace; return true;
		case IBUS_KEY_Return:
		case IBUS_KEY_KP_Enter: *key = Key::kReturn; return true;
		case IBUS_KEY_space:
		case IBUS_KEY_KP_Space: *key = Key::kSpace; return true;
		case IBUS_KEY_Escape: *key = Key::kEscape; return true;
		case IBUS_KEY_Tab:
		case IBUS_KEY_KP_Tab: *key = Key::kTab; return true;
		case IBUS_KEY_ISO_Left_Tab:
			*key = Key::kTab;
			*modifiers |= haikupinyin::kShift;
			return true;
		case IBUS_KEY_Left:
		case IBUS_KEY_KP_Left: *key = Key::kLeft; return true;
		case IBUS_KEY_Right:
		case IBUS_KEY_KP_Right: *key = Key::kRight; return true;
		case IBUS_KEY_Up:
		case IBUS_KEY_KP_Up: *key = Key::kUp; return true;
		case IBUS_KEY_Down:
		case IBUS_KEY_KP_Down: *key = Key::kDown; return true;
		case IBUS_KEY_Page_Up:
		case IBUS_KEY_KP_Page_Up: *key = Key::kPageUp; return true;
		case IBUS_KEY_Page_Down:
		case IBUS_KEY_KP_Page_Down: *key = Key::kPageDown; return true;
		case IBUS_KEY_Home:
		case IBUS_KEY_KP_Home: *key = Key::kHome; return true;
		case IBUS_KEY_End:
		case IBUS_KEY_KP_End: *key = Key::kEnd; return true;
		case IBUS_KEY_Delete:
		case IBUS_KEY_KP_Delete: *key = Key::kDelete; return true;
	}
	gunichar unicode = ibus_keyval_to_unicode(keyval);
	if (unicode >= 0x20 && unicode < 0x7f) {
		*key = Key::kCharacter;
		*character = unicode;
	} else if (unicode >= 0x80) {
		// A non-ASCII character: ends the word, then types itself.
		*key = Key::kCharacter;
		*character = 0x100;
	} else
		*key = Key::kOther;
	return true;
}


static bool
IsModifierKey(guint keyval)
{
	switch (keyval) {
		case IBUS_KEY_Shift_L: case IBUS_KEY_Shift_R:
		case IBUS_KEY_Control_L: case IBUS_KEY_Control_R:
		case IBUS_KEY_Alt_L: case IBUS_KEY_Alt_R:
		case IBUS_KEY_Meta_L: case IBUS_KEY_Meta_R:
		case IBUS_KEY_Super_L: case IBUS_KEY_Super_R:
		case IBUS_KEY_Hyper_L: case IBUS_KEY_Hyper_R:
		case IBUS_KEY_Caps_Lock: case IBUS_KEY_Shift_Lock:
		case IBUS_KEY_ISO_Level3_Shift: case IBUS_KEY_ISO_Level5_Shift:
		case IBUS_KEY_Mode_switch: case IBUS_KEY_Num_Lock:
			return true;
	}
	return false;
}


static void
ToggleChinese(IBusEngine* engine)
{
	sShared->engine.SetChinese(!sShared->engine.Chinese());
	Sync(engine);
	UpdateProperties(engine, true);
	SaveSettings();
}


static gboolean
ProcessKeyEvent(IBusEngine* engine, guint keyval, guint keycode, guint state)
{
	HaikuPinyinEngine* self = HP_ENGINE(engine);
	bool release = (state & IBUS_RELEASE_MASK) != 0;
	Claim(engine);

	// Shift pressed and let go on its own switches 中/英.
	bool shift = keyval == IBUS_KEY_Shift_L || keyval == IBUS_KEY_Shift_R;
	guint others = IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_SUPER_MASK
		| IBUS_MOD4_MASK | IBUS_META_MASK;
	if (shift && !release) {
		self->shiftAlone = (state & others) == 0;
		self->shiftDown = g_get_monotonic_time();
		return FALSE;
	}
	if (shift && release) {
		bool tap = self->shiftAlone
			&& g_get_monotonic_time() - self->shiftDown <= kShiftTapLimit;
		self->shiftAlone = false;
		if (tap && EnsureLoaded())
			ToggleChinese(engine);
		return FALSE;
	}
	if (!release)
		self->shiftAlone = false;

	if (release) {
		if (self->swallowed->erase(keycode) > 0)
			return TRUE;
		return FALSE;
	}
	if (IsModifierKey(keyval) || !EnsureLoaded())
		return FALSE;

	haikupinyin::Engine& pinyin = sShared->engine;
	// Caps Lock types English, as on the Mac.
	if ((state & IBUS_LOCK_MASK) != 0 && !pinyin.Composing())
		return FALSE;

	Key key;
	char32_t character;
	uint32_t modifiers;
	MapKey(keyval, state, &key, &character, &modifiers);
	if (key == Key::kTab && (modifiers & haikupinyin::kShift) != 0)
		key = Key::kPageUp;

	bool used = pinyin.ProcessKey(key, character, modifiers);
	Sync(engine);
	if (used)
		self->swallowed->insert(keycode);
	return used ? TRUE : FALSE;
}


static void
FocusIn(IBusEngine* engine)
{
	Claim(engine);
	UpdateProperties(engine);
	IBUS_ENGINE_CLASS(haiku_pinyin_engine_parent_class)->focus_in(engine);
}


static void
FocusOut(IBusEngine* engine)
{
	if (sShared->loaded && sShared->last == engine) {
		sShared->engine.Reset();
		sShared->engine.ClearContext();
		Sync(engine);
	}
	if (sShared->focused == engine)
		sShared->focused = nullptr;
	IBUS_ENGINE_CLASS(haiku_pinyin_engine_parent_class)->focus_out(engine);
}


// The application moved the cursor or changed the text.
static void
Reset(IBusEngine* engine)
{
	if (sShared->loaded && sShared->last == engine) {
		sShared->engine.Reset();
		sShared->engine.ClearContext();
		Sync(engine);
	}
}


static void
Enable(IBusEngine* engine)
{
	EnsureLoaded();
	Claim(engine);
	UpdateProperties(engine);
}


static void
Disable(IBusEngine* engine)
{
	if (!sShared->loaded)
		return;
	if (sShared->last == engine)
		Cancel(engine);
	sShared->engine.SaveUserDictionary();
}


static void
PageUp(IBusEngine* engine)
{
	if (!sShared->loaded || sShared->last != engine)
		return;
	sShared->engine.ProcessKey(Key::kPageUp, 0, 0);
	ShowCandidates(engine);
}


static void
PageDown(IBusEngine* engine)
{
	if (!sShared->loaded || sShared->last != engine)
		return;
	sShared->engine.ProcessKey(Key::kPageDown, 0, 0);
	ShowCandidates(engine);
}


static void
CursorUp(IBusEngine* engine)
{
	if (!sShared->loaded || sShared->last != engine)
		return;
	sShared->engine.ProcessKey(Key::kLeft, 0, 0);
	ShowCandidates(engine);
}


static void
CursorDown(IBusEngine* engine)
{
	if (!sShared->loaded || sShared->last != engine)
		return;
	sShared->engine.ProcessKey(Key::kRight, 0, 0);
	ShowCandidates(engine);
}


static void
CandidateClicked(IBusEngine* engine, guint index, guint button, guint state)
{
	if (!sShared->loaded || sShared->last != engine)
		return;
	// Panels give the index on the page; guard against ones that don't.
	int pageSize = sShared->engine.GetOptions().pageSize;
	sShared->engine.SelectOnPage(int(index % guint(pageSize)));
	Sync(engine);
}


static void
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


static void
haiku_pinyin_engine_init(HaikuPinyinEngine* self)
{
	self->swallowed = new std::set<guint>;
	self->shiftAlone = false;
	self->shiftDown = 0;
	self->preeditShown = false;
}


static void
haiku_pinyin_engine_destroy(IBusObject* object)
{
	HaikuPinyinEngine* self = HP_ENGINE(object);
	IBusEngine* engine = IBUS_ENGINE(object);
	if (sShared->focused == engine)
		sShared->focused = nullptr;
	if (sShared->last == engine)
		sShared->last = nullptr;
	delete self->swallowed;
	self->swallowed = nullptr;
	IBUS_OBJECT_CLASS(haiku_pinyin_engine_parent_class)->destroy(object);
}


static void
haiku_pinyin_engine_class_init(HaikuPinyinEngineClass* klass)
{
	IBUS_OBJECT_CLASS(klass)->destroy = haiku_pinyin_engine_destroy;
	IBusEngineClass* engine = IBUS_ENGINE_CLASS(klass);
	engine->process_key_event = ProcessKeyEvent;
	engine->focus_in = FocusIn;
	engine->focus_out = FocusOut;
	engine->reset = Reset;
	engine->enable = Enable;
	engine->disable = Disable;
	engine->page_up = PageUp;
	engine->page_down = PageDown;
	engine->cursor_up = CursorUp;
	engine->cursor_down = CursorDown;
	engine->candidate_clicked = CandidateClicked;
	engine->property_activate = PropertyActivate;
}


// #pragma mark - cloud candidates


// One question at a time, asked through curl on a thread so neither IBus nor
// the composition waits for the network; the answer comes back on the main
// loop.
struct CloudAnswer {
	std::string query;
	std::string text;
};


static std::string
AskGoogle(const std::string& query)
{
	// The query is full pinyin: letters only, safe in a URL and a shell.
	std::string command = "curl -s -m 2 'https://inputtools.google.com/"
		"request?text=" + query + "&itc=zh-t-i0-pinyin&num=1&cp=0&cs=1"
		"&ie=utf-8&oe=utf-8&app=haikupinyin' 2>/dev/null";
	FILE* pipe = popen(command.c_str(), "r");
	if (pipe == nullptr)
		return std::string();
	std::string reply;
	char buffer[4096];
	size_t read;
	while ((read = fread(buffer, 1, sizeof(buffer), pipe)) > 0)
		reply.append(buffer, read);
	pclose(pipe);

	// ["SUCCESS",[["<query>",["<answer>", ...
	std::string marker = "[[\"" + query + "\",[\"";
	size_t start = reply.find(marker);
	if (reply.compare(0, 11, "[\"SUCCESS\",") != 0
		|| start == std::string::npos) {
		return std::string();
	}
	start += marker.size();
	size_t end = reply.find('"', start);
	if (end == std::string::npos)
		return std::string();
	std::string answer = reply.substr(start, end - start);
	// Chinese comes back as UTF-8; anything escaped isn't worth offering.
	if (answer.find('\\') != std::string::npos)
		return std::string();
	return answer;
}


static gboolean
CloudAnswered(gpointer data)
{
	CloudAnswer* answer = static_cast<CloudAnswer*>(data);
	sShared->cloudAsked.clear();
	if (sShared->loaded
		&& sShared->engine.SetCloudResult(answer->query, answer->text)
		&& sShared->last != nullptr) {
		ShowCandidates(sShared->last);
	}
	delete answer;
	return G_SOURCE_REMOVE;
}


static void
AskCloud()
{
	if (!sShared->engine.GetOptions().cloud || !sShared->cloudAsked.empty())
		return;
	std::string query = sShared->engine.CloudQuery();
	if (query.empty() || query == sShared->cloudAnswered)
		return;
	for (char c : query) {
		if (!g_ascii_isalpha(c))
			return;
	}
	sShared->cloudAsked = query;
	sShared->cloudAnswered = query;
	std::thread([query]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(kCloudDelay));
		g_idle_add(CloudAnswered, new CloudAnswer{query, AskGoogle(query)});
	}).detach();
}


// #pragma mark - main


static void
Disconnected(IBusBus* bus, gpointer data)
{
	if (sShared->loaded)
		sShared->engine.SaveUserDictionary();
	ibus_quit();
}


static IBusComponent*
NewComponent()
{
	IBusComponent* component = ibus_component_new(kComponentName,
		"海酷拼音 Haiku Pinyin", HP_VERSION, "GPL-3.0",
		"John Zheng <bluevisor@gmail.com>",
		"https://github.com/bluevisor/haiku-pinyin", "", "haiku-pinyin");
	IBusEngineDesc* desc = ibus_engine_desc_new_varargs(
		"name", kEngineName,
		"longname", "海酷拼音 Haiku Pinyin",
		"description", "Chinese pinyin and shuangpin input",
		"language", "zh_CN",
		"license", "GPL-3.0",
		"author", "John Zheng <bluevisor@gmail.com>",
		"icon", HP_DATADIR "/icon.svg",
		"layout", "default",
		"symbol", "中",
		"rank", 99,
		nullptr);
	ibus_component_add_engine(component, desc);
	return component;
}


int
main(int argc, char** argv)
{
	bool launchedByIBus = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--ibus") == 0 || strcmp(argv[i], "-i") == 0)
			launchedByIBus = true;
		else if (strcmp(argv[i], "--version") == 0) {
			printf("haiku-pinyin %s\n", HP_VERSION);
			return 0;
		} else {
			fprintf(stderr, "usage: %s [--ibus]\n", argv[0]);
			return 1;
		}
	}

	sShared = new Shared;
	LoadSettings();

	ibus_init();
	IBusBus* bus = ibus_bus_new();
	if (!ibus_bus_is_connected(bus)) {
		fprintf(stderr, "haiku-pinyin: can't connect to ibus-daemon\n");
		return 1;
	}
	g_signal_connect(bus, "disconnected", G_CALLBACK(Disconnected), nullptr);

	IBusFactory* factory = ibus_factory_new(ibus_bus_get_connection(bus));
	ibus_factory_add_engine(factory, kEngineName,
		haiku_pinyin_engine_get_type());

	if (launchedByIBus)
		ibus_bus_request_name(bus, kComponentName, 0);
	else {
		IBusComponent* component = NewComponent();
		ibus_bus_register_component(bus, component);
		g_object_unref(component);
	}

	// Load the dictionaries now rather than on the first key.
	EnsureLoaded();
	ibus_main();

	if (sShared->loaded)
		sShared->engine.SaveUserDictionary();
	return 0;
}
