// 海酷拼音 Haiku Pinyin for Linux: an IBus engine around the portable engine.
//   ibus-engine-haiku-pinyin --ibus     started by ibus-daemon (component XML)
//   ibus-engine-haiku-pinyin            registers itself, for development:
//                                       then `ibus engine haiku-pinyin`
//
// IBus makes one engine object per input context; they share one
// haikupinyin::Engine (the dictionaries are large), which is reset whenever
// focus moves to another context.

#include <ibus.h>

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "Shared.h"

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

Shared* sShared;


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


// The cloud's answer, back on the main loop.
struct CloudAnswer {
	std::string query;
	std::string text;
};


static gboolean
CloudAnswered(gpointer data)
{
	CloudAnswer* answer = static_cast<CloudAnswer*>(data);
	sShared->cloud.Answered();
	if (sShared->loaded) {
		if (!sShared->engine.SetCloudResult(answer->query, answer->text))
			AskCloud();		// typing moved on while it was asked
		else if (sShared->last != nullptr)
			ShowCandidates(sShared->last);
	}
	delete answer;
	return G_SOURCE_REMOVE;
}


void
AskCloud()
{
	sShared->cloud.Ask(sShared->engine,
		[](const std::string& query, const std::string& text) {
			g_idle_add(CloudAnswered, new CloudAnswer{query, text});
		});
}


void
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
	// Digits, or letters while v-mode takes digits as typing.
	std::string keys = pinyin.PickKeys();
	for (int i = 0; i < pageSize && i < int(keys.size()); i++) {
		ibus_lookup_table_set_label(table, i, ibus_text_new_from_string(
			(std::string(1, keys[i]) + ".").c_str()));
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


void
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


void
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
	// Control+Shift+F switched 简/繁: the menu shows it, and it's saved.
	if (pinyin.TakeScriptToggled()) {
		UpdateProperties(engine, true);
		SaveSettings();
	}
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
	std::string setup = SetupProgram();
	if (!setup.empty())
		g_object_set(desc, "setup", setup.c_str(), nullptr);
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
	WatchSettings();
	ibus_main();

	if (sShared->loaded)
		sShared->engine.SaveUserDictionary();
	return 0;
}
