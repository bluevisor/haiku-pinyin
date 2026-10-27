#include "PinyinMethod.h"

#include <Autolock.h>
#include <Bitmap.h>
#include <File.h>
#include <FindDirectory.h>
#include <Input.h>
#include <InterfaceDefs.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <Path.h>
#include <Screen.h>
#include <View.h>

#include <stdio.h>
#include <sys/stat.h>

#include "CandidateWindow.h"

using haikupinyin::Key;

// The input_server's private per-method record; MethodActivated() is exported.
class _BMethodAddOn_ {
public:
	status_t MethodActivated(bool activate);
};

enum {
	kMsgToggleChinese	= 'hpCn',
	kMsgSetLayout		= 'hpLy',	// "layout" string
	kMsgSetScript		= 'hpSc',	// "script" int32
	kMsgTogglePunctuation = 'hpPu',
	kMsgSetPageSize		= 'hpPg',	// "size" int32
	kMsgToggleEmoji		= 'hpEm',
	kMsgToggleOption	= 'hpOp',	// "option": prediction, english, cloud
	kMsgToggleFuzzy		= 'hpFz',	// "mask" int32
	kMsgCloudResult		= 'hpCl',	// "query", "text"
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

static const bigtime_t kCloudDelay = 120000;	// let typing settle first

static const bigtime_t kShiftTapLimit = 400000;

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


static BPath
SettingsDirectory()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) == B_OK) {
		path.Append("HaikuPinyin");
		mkdir(path.Path(), 0755);
	}
	return path;
}


static BMessage*
NewEvent(int32 opcode)
{
	BMessage* event = new BMessage(B_INPUT_METHOD_EVENT);
	event->AddInt32("be:opcode", opcode);
	// Views that don't speak the protocol only get the committed text, as
	// typing (be:translated); the candidate window shows the rest.
	event->AddBool("be:inline_only", true);
	return event;
}


// The committed text as an ordinary key press, for views that aren't input
// method aware.
static BMessage
TypedText(const std::string& text)
{
	BMessage typed(B_KEY_DOWN);
	typed.AddInt64("when", system_time());
	typed.AddInt32("modifiers", 0);
	typed.AddInt32("key", 0);
	typed.AddInt32("raw_char", (uint8)text[0]);
	typed.AddInt8("byte", (int8)text[0]);
	typed.AddString("bytes", text.c_str());
	return typed;
}


// #pragma mark - MethodLooper


MethodLooper::MethodLooper(PinyinMethod* method)
	:
	BLooper("海酷拼音"),
	fMethod(method)
{
}


void
MethodLooper::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_INPUT_METHOD_EVENT:
		case kMsgPickCandidate:
		case kMsgToggleChinese:
		case kMsgSetLayout:
		case kMsgSetScript:
		case kMsgTogglePunctuation:
		case kMsgSetPageSize:
		case kMsgToggleEmoji:
		case kMsgToggleOption:
		case kMsgToggleFuzzy:
		case kMsgCloudResult:
		case kMsgTurnPage:
			fMethod->LooperMessage(message);
			break;
		default:
			BLooper::MessageReceived(message);
	}
}


// #pragma mark - PinyinMethod


PinyinMethod::PinyinMethod()
	:
	BInputServerMethod("海酷拼音", NULL),
	fLock("haiku pinyin"),
	fSavedChinese(true),
	fMenu(NULL),
	fLoaded(false),
	fLoadFailed(false),
	fActive(false),
	fLooper(NULL),
	fWindow(NULL),
	fSessionOpen(false),
	fShownPicked(0),
	fMouse(100, 100),
	fShiftAlone(false),
	fShiftDown(0)
{
	fLooper = new MethodLooper(this);
	fLooper->Run();
	_LoadSettings();
	// Deskbar paints the stored icon before MethodActivated() runs, so it
	// has to be right before the first switch.
	_UpdateIcon();
}


PinyinMethod::~PinyinMethod()
{
	BAutolock _(fLock);
	if (fLoaded)
		fEngine.SaveUserDictionary();
	if (fWindow != NULL && fWindow->Lock())
		fWindow->Quit();
	if (fLooper->Lock())
		fLooper->Quit();
}


status_t
PinyinMethod::InitCheck()
{
	return B_OK;
}


bool
PinyinMethod::_EnsureLoaded()
{
	if (fLoaded || fLoadFailed)
		return fLoaded;

	static const directory_which kPlaces[] = {
		B_USER_NONPACKAGED_DATA_DIRECTORY, B_USER_DATA_DIRECTORY,
		B_SYSTEM_NONPACKAGED_DATA_DIRECTORY, B_SYSTEM_DATA_DIRECTORY
	};
	std::string error = "no haiku-pinyin data directory";
	for (directory_which place : kPlaces) {
		BPath path;
		if (find_directory(place, &path) != B_OK)
			continue;
		path.Append("haiku-pinyin");
		if (fEngine.Load(path.Path(), &error)) {
			fLoaded = true;
			break;
		}
	}
	if (!fLoaded) {
		fprintf(stderr, "Haiku Pinyin: %s\n", error.c_str());
		fLoadFailed = true;
		return false;
	}

	BPath user = SettingsDirectory();
	user.Append("user.dict");
	fEngine.SetUserDictionary(user.Path());

	// Only now can the layout the settings name be checked.
	fEngine.SetOptions(fSavedOptions);
	fEngine.SetChinese(fSavedChinese);

	fWindow = new CandidateWindow(BMessenger(fLooper));
	fWindow->Hide();
	fWindow->Show();	// runs it, still hidden
	return true;
}


status_t
PinyinMethod::MethodActivated(bool active)
{
	BList events;
	{
		BAutolock _(fLock);
		fActive = active;
		if (active) {
			_EnsureLoaded();
			_UpdateIcon();
			_UpdateMenu();
		} else if (fLoaded) {
			_Cancel(&events);
			fEngine.SaveUserDictionary();
		}
	}
	_Deliver(events);
	return B_OK;
}


filter_result
PinyinMethod::Filter(BMessage* message, BList* outList)
{
	switch (message->what) {
		case B_MOUSE_MOVED:
			message->FindPoint("where", &fMouse);
			return B_DISPATCH_MESSAGE;
		case B_MOUSE_DOWN:
		{
			message->FindPoint("where", &fMouse);
			// A click elsewhere: what was typed before no longer leads.
			BAutolock _(fLock);
			if (fLoaded && fEngine.Predicting()) {
				fEngine.ClearContext();
				_ShowCandidates();
			}
			return B_DISPATCH_MESSAGE;
		}
		case B_MODIFIERS_CHANGED:
			if (_HandleModifiers(message)) {
				BList events;
				{
					BAutolock _(fLock);
					_Sync(&events);
				}
				outList->AddList(&events);
				outList->AddItem(new BMessage(*message));
			}
			return B_DISPATCH_MESSAGE;
		case B_KEY_UP:
		case B_UNMAPPED_KEY_UP:
		{
			int32 key = message->GetInt32("key", -1);
			if (fSwallowedKeys.erase(key) > 0)
				return B_SKIP_MESSAGE;
			return B_DISPATCH_MESSAGE;
		}
		case B_KEY_DOWN:
			break;
		default:
			return B_DISPATCH_MESSAGE;
	}

	fShiftAlone = false;
	BAutolock _(fLock);
	if (!fActive || !_EnsureLoaded())
		return B_DISPATCH_MESSAGE;

	bool used = _HandleKey(message);
	BList events;
	_Sync(&events);
	if (used)
		fSwallowedKeys.insert(message->GetInt32("key", -1));

	if (events.IsEmpty())
		return used ? B_SKIP_MESSAGE : B_DISPATCH_MESSAGE;
	// Our events replace the key; a key we didn't use follows them.
	outList->AddList(&events);
	if (!used)
		outList->AddItem(new BMessage(*message));
	return B_DISPATCH_MESSAGE;
}


bool
PinyinMethod::_HandleKey(BMessage* message)
{
	const char* bytes = message->GetString("bytes", "");
	uint32 haikuModifiers = message->GetInt32("modifiers", 0);

	uint32 modifiers = 0;
	if (haikuModifiers & B_SHIFT_KEY)
		modifiers |= haikupinyin::kShift;
	if (haikuModifiers & B_CONTROL_KEY)
		modifiers |= haikupinyin::kControl;
	if (haikuModifiers & B_COMMAND_KEY)
		modifiers |= haikupinyin::kCommand;
	if (haikuModifiers & B_OPTION_KEY)
		modifiers |= haikupinyin::kOption;

	// Caps Lock types English, as on the Mac.
	if ((haikuModifiers & B_CAPS_LOCK) != 0 && !fEngine.Composing())
		return false;

	Key key = Key::kCharacter;
	char32_t character = 0;
	uint8 first = (uint8)bytes[0];
	if (bytes[0] != '\0' && bytes[1] == '\0') {
		switch (first) {
			case B_BACKSPACE: key = Key::kBackspace; break;
			case B_RETURN: key = Key::kReturn; break;
			case B_SPACE: key = Key::kSpace; break;
			case B_ESCAPE: key = Key::kEscape; break;
			case B_TAB: key = Key::kTab; break;
			case B_LEFT_ARROW: key = Key::kLeft; break;
			case B_RIGHT_ARROW: key = Key::kRight; break;
			case B_UP_ARROW: key = Key::kUp; break;
			case B_DOWN_ARROW: key = Key::kDown; break;
			case B_PAGE_UP: key = Key::kPageUp; break;
			case B_PAGE_DOWN: key = Key::kPageDown; break;
			case B_HOME: key = Key::kHome; break;
			case B_END: key = Key::kEnd; break;
			case B_DELETE: key = Key::kDelete; break;
			default:
				if (first >= 0x20 && first < 0x7f)
					character = first;
				else
					key = Key::kOther;
		}
	} else if (bytes[0] != '\0') {
		// A non-ASCII character: ends the word, then types itself.
		character = 0x100;
	} else
		key = Key::kOther;

	if (key == Key::kTab && (modifiers & haikupinyin::kShift) != 0)
		key = Key::kPageUp;
	return fEngine.ProcessKey(key, character, modifiers);
}


// Shift pressed and let go on its own switches 中/英.
bool
PinyinMethod::_HandleModifiers(BMessage* message)
{
	uint32 now = message->GetInt32("modifiers", 0);
	uint32 old = message->GetInt32("be:old_modifiers", 0);
	uint32 others = B_CONTROL_KEY | B_COMMAND_KEY | B_OPTION_KEY | B_MENU_KEY;

	if ((now & B_SHIFT_KEY) != 0 && (old & B_SHIFT_KEY) == 0) {
		fShiftAlone = (now & others) == 0;
		fShiftDown = system_time();
		return false;
	}
	if ((now & others) != 0)
		fShiftAlone = false;
	if ((now & B_SHIFT_KEY) == 0 && (old & B_SHIFT_KEY) != 0 && fShiftAlone) {
		fShiftAlone = false;
		if (system_time() - fShiftDown > kShiftTapLimit)
			return false;
		BAutolock _(fLock);
		if (!fActive || !_EnsureLoaded())
			return false;
		fEngine.SetChinese(!fEngine.Chinese());
		_UpdateIcon();
		_UpdateMenu();
		_RepaintDeskbar();
		_SaveSettings();
		return true;
	}
	return false;
}


// Turns the engine's state into input method events: what to commit, the
// inline composition, the candidate window. Called with fLock held.
void
PinyinMethod::_Sync(BList* events)
{
	std::string commit = fEngine.TakeCommit();
	if (!commit.empty()) {
		if (!fSessionOpen) {
			BMessage* started = NewEvent(B_INPUT_METHOD_STARTED);
			started->AddMessenger("be:reply_to", BMessenger(fLooper));
			events->AddItem(started);
		}
		BMessage* changed = NewEvent(B_INPUT_METHOD_CHANGED);
		changed->AddString("be:string", commit.c_str());
		changed->AddBool("be:confirmed", true);
		BMessage typed = TypedText(commit);
		changed->AddMessage("be:translated", &typed);
		events->AddItem(changed);
		events->AddItem(NewEvent(B_INPUT_METHOD_STOPPED));
		fSessionOpen = false;
		fShownPreedit.clear();
		fShownPicked = 0;
	}

	if (fEngine.Composing()) {
		std::string preedit = fEngine.Preedit();
		size_t picked = fEngine.PickedLength();
		bool locate = !fSessionOpen || picked != fShownPicked;
		if (!fSessionOpen) {
			BMessage* started = NewEvent(B_INPUT_METHOD_STARTED);
			started->AddMessenger("be:reply_to", BMessenger(fLooper));
			events->AddItem(started);
			fSessionOpen = true;
		}
		if (preedit != fShownPreedit) {
			BMessage* changed = NewEvent(B_INPUT_METHOD_CHANGED);
			changed->AddString("be:string", preedit.c_str());
			changed->AddBool("be:confirmed", false);
			// Picked text and the pinyin still being typed are two clauses;
			// the caret sits at the end.
			changed->AddInt32("be:clause_start", 0);
			changed->AddInt32("be:clause_end", (int32)picked);
			changed->AddInt32("be:clause_start", (int32)picked);
			changed->AddInt32("be:clause_end", (int32)preedit.size());
			changed->AddInt32("be:selection", (int32)preedit.size());
			changed->AddInt32("be:selection", (int32)preedit.size());
			events->AddItem(changed);
			fShownPreedit = preedit;
		}
		if (locate)
			events->AddItem(NewEvent(B_INPUT_METHOD_LOCATION_REQUEST));
		fShownPicked = picked;
	} else if (fSessionOpen) {
		// Cancelled: clear the inline text.
		BMessage* changed = NewEvent(B_INPUT_METHOD_CHANGED);
		changed->AddString("be:string", "");
		changed->AddBool("be:confirmed", true);
		events->AddItem(changed);
		events->AddItem(NewEvent(B_INPUT_METHOD_STOPPED));
		fSessionOpen = false;
		fShownPreedit.clear();
		fShownPicked = 0;
	}

	_ShowCandidates();
}


void
PinyinMethod::_Cancel(BList* events)
{
	fEngine.Reset();
	_Sync(events);
}


void
PinyinMethod::_Deliver(BList& events)
{
	for (int32 i = 0; i < events.CountItems(); i++) {
		BMessage* event = (BMessage*)events.ItemAt(i);
		if (EnqueueMessage(event) != B_OK)
			delete event;
	}
}


void
PinyinMethod::_ShowCandidates()
{
	if (fWindow == NULL)
		return;
	if (!fEngine.Composing() && !fEngine.Predicting()) {
		fWindow->PostMessage(kMsgHideCandidates);
		return;
	}
	_AskCloud();
	BMessage show(kMsgShowCandidates);
	show.AddString("spelled",
		fEngine.Composing() ? fEngine.Spelled().c_str() : "");
	show.AddInt32("cloud", fEngine.CloudIndexOnPage());
	for (const std::string& candidate : fEngine.PageCandidates())
		show.AddString("candidate", candidate.c_str());
	show.AddInt32("highlight", fEngine.Highlighted());
	show.AddBool("previous", fEngine.HasPreviousPage());
	show.AddBool("next", fEngine.HasNextPage());
	show.AddPoint("mouse", fMouse);
	fWindow->PostMessage(&show);
}


void
PinyinMethod::LooperMessage(BMessage* message)
{
	BList events;
	{
		BAutolock _(fLock);
		if (!fLoaded)
			return;
		haikupinyin::Options options = fEngine.GetOptions();
		switch (message->what) {
			case B_INPUT_METHOD_EVENT:
			{
				int32 opcode = message->GetInt32("be:opcode", -1);
				if (opcode == B_INPUT_METHOD_LOCATION_REQUEST) {
					BPoint where;
					if (message->FindPoint("be:location_reply", &where) == B_OK
						&& fWindow != NULL) {
						BMessage place(kMsgPlaceCandidates);
						place.AddPoint("where", where);
						place.AddFloat("height",
							message->GetFloat("be:height_reply", 16));
						fWindow->PostMessage(&place);
					}
				} else if (opcode == B_INPUT_METHOD_STOPPED) {
					// The application ended it (a click, losing focus); it
					// keeps the inline text.
					fEngine.Reset();
					fEngine.ClearContext();
					fSessionOpen = false;
					fShownPreedit.clear();
					fShownPicked = 0;
					_ShowCandidates();
				}
				return;
			}
			case kMsgPickCandidate:
				fEngine.SelectOnPage(message->GetInt32("index", -1));
				_Sync(&events);
				break;
			case kMsgToggleChinese:
				fEngine.SetChinese(!fEngine.Chinese());
				_Sync(&events);
				_UpdateIcon();
				_RepaintDeskbar();
				break;
			case kMsgSetLayout:
				options.layout = message->GetString("layout", "full");
				_Cancel(&events);
				fEngine.SetOptions(options);
				break;
			case kMsgSetScript:
				options.script = (haikupinyin::Script)message->GetInt32("script", 0);
				_Cancel(&events);
				fEngine.SetOptions(options);
				break;
			case kMsgTogglePunctuation:
				options.fullwidthPunctuation = !options.fullwidthPunctuation;
				fEngine.SetOptions(options);
				break;
			case kMsgSetPageSize:
				options.pageSize = message->GetInt32("size", 7);
				_Cancel(&events);
				fEngine.SetOptions(options);
				break;
			case kMsgToggleEmoji:
				options.emoji = !options.emoji;
				_Cancel(&events);
				fEngine.SetOptions(options);
				break;
			case kMsgToggleOption:
			{
				BString option = message->GetString("option", "");
				if (option == "prediction")
					options.prediction = !options.prediction;
				else if (option == "english")
					options.english = !options.english;
				else if (option == "cloud")
					options.cloud = !options.cloud;
				_Cancel(&events);
				fEngine.SetOptions(options);
				break;
			}
			case kMsgToggleFuzzy:
				options.fuzzy ^= (unsigned)message->GetInt32("mask", 0);
				_Cancel(&events);
				fEngine.SetOptions(options);
				break;
			case kMsgCloudResult:
				fCloudAsked.clear();
				if (fEngine.SetCloudResult(message->GetString("query", ""),
						message->GetString("text", ""))) {
					_ShowCandidates();
				}
				return;
			case kMsgTurnPage:
				fEngine.ProcessKey(message->GetInt32("delta", 1) > 0
					? haikupinyin::Key::kPageDown : haikupinyin::Key::kPageUp,
					0, 0);
				_ShowCandidates();
				return;
		}
		_UpdateMenu();
		_SaveSettings();
	}
	_Deliver(events);
}


void
PinyinMethod::_UpdateMenu()
{
	BMenu* menu = new BMenu("海酷拼音");
	const haikupinyin::Options& options = fEngine.GetOptions();

	BMenuItem* item = new BMenuItem(
		fEngine.Chinese() ? "中文 (Shift 切换)" : "English (Shift 切换)",
		new BMessage(kMsgToggleChinese));
	menu->AddItem(item);
	menu->AddSeparatorItem();

	for (const auto& layout : kLayouts) {
		BMessage* message = new BMessage(kMsgSetLayout);
		message->AddString("layout", layout.layout);
		item = new BMenuItem(layout.label, message);
		item->SetMarked(options.layout == layout.layout);
		menu->AddItem(item);
	}
	menu->AddSeparatorItem();

	for (int32 i = 0; i < 2; i++) {
		BMessage* message = new BMessage(kMsgSetScript);
		message->AddInt32("script", i);
		item = new BMenuItem(kScripts[i], message);
		item->SetMarked((int32)options.script == i);
		menu->AddItem(item);
	}
	menu->AddSeparatorItem();

	item = new BMenuItem("全角标点", new BMessage(kMsgTogglePunctuation));
	item->SetMarked(options.fullwidthPunctuation);
	menu->AddItem(item);

	item = new BMenuItem("Emoji 候选", new BMessage(kMsgToggleEmoji));
	item->SetMarked(options.emoji);
	menu->AddItem(item);

	static const struct {
		const char* option;
		const char* label;
	} kToggles[] = {
		{"prediction", "联想"},
		{"english", "中英混输"},
		{"cloud", "云候选 (Google)"},
	};
	for (const auto& toggle : kToggles) {
		BMessage* message = new BMessage(kMsgToggleOption);
		message->AddString("option", toggle.option);
		item = new BMenuItem(toggle.label, message);
		BString option = toggle.option;
		item->SetMarked(option == "prediction" ? options.prediction
			: option == "english" ? options.english : options.cloud);
		menu->AddItem(item);
	}
	menu->AddSeparatorItem();

	for (const auto& group : kFuzzyGroups) {
		BMessage* message = new BMessage(kMsgToggleFuzzy);
		message->AddInt32("mask", (int32)group.mask);
		item = new BMenuItem(group.label, message);
		item->SetMarked((options.fuzzy & group.mask) == group.mask);
		menu->AddItem(item);
	}
	menu->AddSeparatorItem();

	// No submenus: Deskbar only targets the top level of a method's menu
	// (SetTargetForItems doesn't recurse), so a submenu's picks get lost.
	for (int32 size : {5, 7, 9}) {
		BMessage* message = new BMessage(kMsgSetPageSize);
		message->AddInt32("size", size);
		BString label;
		label << "每页 " << size << " 个候选";
		item = new BMenuItem(label.String(), message);
		item->SetMarked(options.pageSize == size);
		menu->AddItem(item);
	}

	// The add-on only keeps (and finally deletes) the latest menu.
	SetMenu(menu, BMessenger(fLooper));
	delete fMenu;
	fMenu = menu;
}


// The Deskbar icon: 中 on blue, or 英 on gray, as a 16x16 CMAP8 bitmap.
void
PinyinMethod::_UpdateIcon()
{
	bool chinese = fLoaded ? fEngine.Chinese() : fSavedChinese;
	BRect bounds(0, 0, 15, 15);
	BBitmap canvas(bounds, B_RGB32, true);
	if (canvas.InitCheck() != B_OK)
		return;
	BView* view = new BView(bounds, "icon", 0, 0);
	canvas.Lock();
	canvas.AddChild(view);
	view->SetHighColor(chinese ? make_color(0x2f, 0x6f, 0xd6)
		: make_color(0x80, 0x80, 0x88));
	view->FillRect(bounds);
	BFont font(be_bold_font);
	font.SetSize(13);
	view->SetFont(&font);
	view->SetHighColor(255, 255, 255);
	const char* glyph = chinese ? "中" : "英";
	font_height height;
	font.GetHeight(&height);
	float width = font.StringWidth(glyph);
	view->SetDrawingMode(B_OP_OVER);
	view->DrawString(glyph, BPoint((16 - width) / 2,
		(16 + height.ascent - height.descent) / 2));
	view->Sync();
	canvas.RemoveChild(view);
	canvas.Unlock();
	delete view;

	uchar icon[16 * 16];
	BScreen screen;
	const uint8* bits = (const uint8*)canvas.Bits();
	for (int y = 0; y < 16; y++) {
		const uint8* row = bits + y * canvas.BytesPerRow();
		for (int x = 0; x < 16; x++) {
			const uint8* pixel = row + x * 4;	// B G R A
			icon[y * 16 + x] = screen.IndexForColor(pixel[2], pixel[1],
				pixel[0]);
		}
	}
	// Rounded corners.
	for (int corner : {0, 15, 240, 255})
		icon[corner] = B_TRANSPARENT_MAGIC_CMAP8;
	SetIcon(icon);
}


// SetIcon() only reaches the method's menu item; Deskbar repaints its icon
// when told a method was activated. The add-on's record does that, then
// calls MethodActivated(true) here again, which is harmless while active.
void
PinyinMethod::_RepaintDeskbar()
{
	// fOwner is BInputServerMethod's last member.
	_BMethodAddOn_* owner = *(_BMethodAddOn_**)((char*)
		static_cast<BInputServerMethod*>(this)
		+ sizeof(BInputServerMethod) - sizeof(_BMethodAddOn_*));
	if (fActive && owner != NULL)
		owner->MethodActivated(true);
}


void
PinyinMethod::_LoadSettings()
{
	BPath path = SettingsDirectory();
	path.Append("settings");
	BFile file(path.Path(), B_READ_ONLY);
	BMessage settings;
	if (file.InitCheck() != B_OK || settings.Unflatten(&file) != B_OK)
		return;
	fSavedOptions.layout = settings.GetString("layout", "full");
	// Only 简体 and 繁體 are offered; older settings may name a regional
	// variant.
	fSavedOptions.script = settings.GetInt32("script", 0) == 0
		? haikupinyin::Script::kSimplified : haikupinyin::Script::kTraditional;
	fSavedOptions.pageSize = settings.GetInt32("page size", 7);
	fSavedOptions.fullwidthPunctuation = settings.GetBool("fullwidth", true);
	fSavedOptions.emoji = settings.GetBool("emoji", true);
	fSavedOptions.prediction = settings.GetBool("prediction", true);
	fSavedOptions.english = settings.GetBool("english", true);
	fSavedOptions.cloud = settings.GetBool("cloud", false);
	fSavedOptions.fuzzy = (unsigned)settings.GetInt32("fuzzy", 0);
	fSavedChinese = settings.GetBool("chinese", true);
}


void
PinyinMethod::_SaveSettings()
{
	const haikupinyin::Options& options = fEngine.GetOptions();
	BMessage settings;
	settings.AddString("layout", options.layout.c_str());
	settings.AddInt32("script", (int32)options.script);
	settings.AddInt32("page size", options.pageSize);
	settings.AddBool("fullwidth", options.fullwidthPunctuation);
	settings.AddBool("emoji", options.emoji);
	settings.AddBool("prediction", options.prediction);
	settings.AddBool("english", options.english);
	settings.AddBool("cloud", options.cloud);
	settings.AddInt32("fuzzy", (int32)options.fuzzy);
	settings.AddBool("chinese", fEngine.Chinese());

	BPath path = SettingsDirectory();
	path.Append("settings");
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		settings.Flatten(&file);
}


// Cloud candidates: one question at a time, asked through curl on a thread
// so neither the input_server nor the composition waits for the network.
struct CloudQuestion {
	BMessenger reply;
	std::string query;
};


static std::string
CloudAnswer(const std::string& query)
{
	// The query is full pinyin: letters only, safe in a URL and a shell.
	std::string command = "curl -s -m 2 'https://inputtools.google.com/"
		"request?text=" + query + "&itc=zh-t-i0-pinyin&num=1&cp=0&cs=1"
		"&ie=utf-8&oe=utf-8&app=haikupinyin' 2>/dev/null";
	FILE* pipe = popen(command.c_str(), "r");
	if (pipe == NULL)
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
	if (reply.compare(0, 11, "[\"SUCCESS\",") != 0 || start == std::string::npos)
		return std::string();
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


static status_t
AskCloud(void* data)
{
	CloudQuestion* question = (CloudQuestion*)data;
	snooze(kCloudDelay);
	BMessage result(kMsgCloudResult);
	result.AddString("query", question->query.c_str());
	result.AddString("text", CloudAnswer(question->query).c_str());
	question->reply.SendMessage(&result);
	delete question;
	return B_OK;
}


// Called with fLock held.
void
PinyinMethod::_AskCloud()
{
	if (!fEngine.GetOptions().cloud || !fCloudAsked.empty())
		return;
	std::string query = fEngine.CloudQuery();
	if (query.empty() || query == fCloudAnswered)
		return;
	fCloudAsked = query;
	fCloudAnswered = query;
	thread_id thread = spawn_thread(AskCloud, "haiku pinyin cloud",
		B_LOW_PRIORITY, new CloudQuestion{BMessenger(fLooper), query});
	if (thread >= 0)
		resume_thread(thread);
	else
		fCloudAsked.clear();
}


extern "C" BInputServerMethod*
instantiate_input_method()
{
	return new(std::nothrow) PinyinMethod();
}
