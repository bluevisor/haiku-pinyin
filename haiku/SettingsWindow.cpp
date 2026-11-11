// Haiku Pinyin: the settings window. The same options as the Deskbar menu,
// in boxes as Haiku's preferences have them, plus the 词库 actions and a
// problem report.

#include "SettingsWindow.h"

#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <String.h>
#include <StringView.h>

#include <sys/utsname.h>

#include <string>

#include "../engine/Report.h"

#ifndef HP_VERSION
#define HP_VERSION "0"
#endif

enum {
	kMsgChanged	= 'hpCh',	// a control, to the window
	kMsgReport	= 'hpRp',
};

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

// In the order of haikupinyin::Vowels.
static const char* const kVowels[] = { "aa oo ee", "oa oo oe", "a o e 单键" };

static const char* const kScripts[] = { "简体中文", "繁體中文" };

static const int32 kSmallestPage = 3;
static const int32 kLargestPage = 10;

static const struct {
	unsigned mask;
	const char* label;
} kFuzzyGroups[] = {
	{haikupinyin::kFuzzyRetroflex, "z=zh  c=ch  s=sh"},
	{haikupinyin::kFuzzyNasal, "an=ang  en=eng  in=ing"},
	{haikupinyin::kFuzzyNL | haikupinyin::kFuzzyFH | haikupinyin::kFuzzyRL,
		"n=l  f=h  r=l"},
};


void
StoreOptions(BMessage* message, const haikupinyin::Options& options)
{
	message->AddString("layout", options.layout.c_str());
	message->AddInt32("script", (int32)options.script);
	message->AddInt32("page size", options.pageSize);
	message->AddBool("fullwidth", options.fullwidthPunctuation);
	message->AddBool("emoji", options.emoji);
	message->AddBool("english", options.english);
	message->AddBool("cloud", options.cloud);
	message->AddInt32("fuzzy", (int32)options.fuzzy);
	message->AddInt32("vowels", (int32)options.vowels);
}


haikupinyin::Options
LoadOptions(const BMessage& message)
{
	haikupinyin::Options options;
	options.layout = message.GetString("layout", options.layout.c_str());
	// Only 简体 and 繁體 are offered; older settings may name a regional
	// variant.
	options.script = message.GetInt32("script", 0) == 0
		? haikupinyin::Script::kSimplified : haikupinyin::Script::kTraditional;
	options.pageSize = message.GetInt32("page size", options.pageSize);
	options.fullwidthPunctuation = message.GetBool("fullwidth",
		options.fullwidthPunctuation);
	options.emoji = message.GetBool("emoji", options.emoji);
	options.english = message.GetBool("english", options.english);
	options.cloud = message.GetBool("cloud", options.cloud);
	options.fuzzy = (unsigned)message.GetInt32("fuzzy", 0);
	int32 vowels = message.GetInt32("vowels", 0);
	if (vowels >= 0 && vowels <= (int32)haikupinyin::Vowels::kSingle)
		options.vowels = (haikupinyin::Vowels)vowels;
	return options;
}


static BMenuField*
PopUp(const char* name, const char* label)
{
	return new BMenuField(name, label, new BPopUpMenu(name));
}


static void
AddChoice(BMenuField* field, const char* label)
{
	field->Menu()->AddItem(new BMenuItem(label, new BMessage(kMsgChanged)));
}


static void
Choose(BMenuField* field, int32 index)
{
	if (BMenuItem* item = field->Menu()->ItemAt(index))
		item->SetMarked(true);
}


static int32
Chosen(BMenuField* field)
{
	return field->Menu()->IndexOf(field->Menu()->FindMarked());
}


static BCheckBox*
CheckBox(const char* name, const char* label)
{
	return new BCheckBox(name, label, new BMessage(kMsgChanged));
}


// To the method's looper, for the words and phrases.
static BButton*
MethodButton(const char* name, const char* label, uint32 what,
	BMessenger method)
{
	BButton* button = new BButton(name, label, new BMessage(what));
	button->SetTarget(method);
	button->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	return button;
}


static BBox*
Box(const char* label, BView* contents)
{
	BBox* box = new BBox(label);
	box->SetLabel(label);
	box->AddChild(contents);
	return box;
}


SettingsWindow::SettingsWindow(BMessenger method,
	const haikupinyin::Options& options)
	:
	BWindow(BRect(0, 0, 100, 100), "海酷拼音设置", B_TITLED_WINDOW,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_ASYNCHRONOUS_CONTROLS | B_CLOSE_ON_ESCAPE),
	fMethod(method),
	fOptions(options)
{
	fLayout = PopUp("layout", "全拼 / 双拼:");
	for (const auto& layout : kLayouts)
		AddChoice(fLayout, layout.label);
	fVowels = PopUp("vowels", "双拼 a o e:");
	for (const char* vowels : kVowels)
		AddChoice(fVowels, vowels);
	fScript = PopUp("script", "简体 / 繁體:");
	for (const char* script : kScripts)
		AddChoice(fScript, script);
	fPageSize = PopUp("page", "每页候选数:");
	for (int32 size = kSmallestPage; size <= kLargestPage; size++)
		AddChoice(fPageSize, std::to_string(size).c_str());

	fFullwidth = CheckBox("fullwidth", "全角标点");
	fEmoji = CheckBox("emoji", "Emoji 候选");
	fEnglish = CheckBox("english", "中英混输");
	fCloud = CheckBox("cloud", "云候选 (Google)");
	BStringView* cloudNote = new BStringView("cloud note",
		"把正在输入的拼音发给 Google 输入工具，不含已上屏的文字。");
	BFont small(be_plain_font);
	small.SetSize(small.Size() * 0.85f);
	cloudNote->SetFont(&small);
	for (int i = 0; i < 3; i++)
		fFuzzy[i] = CheckBox("fuzzy", kFuzzyGroups[i].label);

	BView* input = BLayoutBuilder::Grid<>(B_USE_DEFAULT_SPACING,
			B_USE_SMALL_SPACING)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddMenuField(fLayout, 0, 0)
		.AddMenuField(fVowels, 0, 1)
		.AddMenuField(fScript, 0, 2)
		.Add(fFullwidth, 1, 3)
		.View();
	BView* candidates = BLayoutBuilder::Group<>(B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGrid(B_USE_DEFAULT_SPACING, 0)
			.AddMenuField(fPageSize, 0, 0)
			.AddGlue(2, 0)
		.End()
		.Add(fEmoji)
		.Add(fEnglish)
		.Add(fCloud)
		.AddGroup(B_HORIZONTAL, 0)
			.AddStrut(be_control_look->DefaultLabelSpacing() * 4)
			.Add(cloudNote)
		.End()
		.View();
	BView* fuzzy = BLayoutBuilder::Group<>(B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fFuzzy[0])
		.Add(fFuzzy[1])
		.Add(fFuzzy[2])
		.View();
	BView* words = BLayoutBuilder::Group<>(B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(MethodButton("phrases", "自定义短语…", kMsgEditPhrases, method))
		.Add(MethodButton("import", "导入词库…", kMsgShowImport, method))
		.Add(MethodButton("export", "导出学到的词…", kMsgShowExport, method))
		.Add(MethodButton("folder", "打开词库文件夹", kMsgOpenFolder, method))
		.View();

	BString version;
	version << "版本 " << HP_VERSION;
	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING, 1.0f)
			.AddGroup(B_VERTICAL)
				.Add(Box("输入", input))
				.Add(Box("候选", candidates))
				.AddGlue()
			.End()
			.AddGroup(B_VERTICAL)
				.Add(Box("模糊音", fuzzy))
				.Add(Box("词库", words))
				.AddGlue()
			.End()
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(new BStringView("version", version.String()))
			.AddGlue()
			.Add(new BButton("report", "报告问题…", new BMessage(kMsgReport)))
		.End();

	_Show(options);
	BSize size = GetLayout()->PreferredSize();
	ResizeTo(size.width, size.height);
	CenterOnScreen();
}


void
SettingsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgChanged:
		{
			fOptions = _Chosen();
			fVowels->SetEnabled(fOptions.layout != "full");
			BMessage apply(kMsgApplySettings);
			StoreOptions(&apply, fOptions);
			fMethod.SendMessage(&apply);
			break;
		}
		case kMsgSettingsChanged:
			fOptions = LoadOptions(*message);
			_Show(fOptions);
			break;
		case kMsgShowSettings:
			Activate();
			break;
		case kMsgReport:
			_Report();
			break;
		default:
			BWindow::MessageReceived(message);
	}
}


void
SettingsWindow::_Show(const haikupinyin::Options& options)
{
	int32 layout = 0;
	for (int32 i = 0; i < (int32)(sizeof(kLayouts) / sizeof(kLayouts[0])); i++) {
		if (options.layout == kLayouts[i].layout)
			layout = i;
	}
	Choose(fLayout, layout);
	Choose(fVowels, (int32)options.vowels);
	fVowels->SetEnabled(options.layout != "full");
	Choose(fScript, options.script == haikupinyin::Script::kSimplified ? 0 : 1);
	Choose(fPageSize, options.pageSize - kSmallestPage);
	fFullwidth->SetValue(options.fullwidthPunctuation);
	fEmoji->SetValue(options.emoji);
	fEnglish->SetValue(options.english);
	fCloud->SetValue(options.cloud);
	for (int i = 0; i < 3; i++) {
		unsigned mask = kFuzzyGroups[i].mask;
		fFuzzy[i]->SetValue((options.fuzzy & mask) == mask);
	}
}


// What the controls say; anything they don't show stays as it was.
haikupinyin::Options
SettingsWindow::_Chosen() const
{
	haikupinyin::Options options = fOptions;
	int32 layout = Chosen(fLayout);
	if (layout >= 0)
		options.layout = kLayouts[layout].layout;
	int32 vowels = Chosen(fVowels);
	if (vowels >= 0)
		options.vowels = (haikupinyin::Vowels)vowels;
	options.script = Chosen(fScript) == 1
		? haikupinyin::Script::kTraditional : haikupinyin::Script::kSimplified;
	int32 page = Chosen(fPageSize);
	if (page >= 0)
		options.pageSize = kSmallestPage + page;
	options.fullwidthPunctuation = fFullwidth->Value() == B_CONTROL_ON;
	options.emoji = fEmoji->Value() == B_CONTROL_ON;
	options.english = fEnglish->Value() == B_CONTROL_ON;
	options.cloud = fCloud->Value() == B_CONTROL_ON;
	for (int i = 0; i < 3; i++) {
		if (fFuzzy[i]->Value() == B_CONTROL_ON)
			options.fuzzy |= kFuzzyGroups[i].mask;
		else
			options.fuzzy &= ~kFuzzyGroups[i].mask;
	}
	return options;
}


// A new GitHub issue in the browser, with the version, the system and the
// settings filled in; never anything typed or learned.
void
SettingsWindow::_Report()
{
	std::string system = "Haiku";
	struct utsname name;
	if (uname(&name) == 0) {
		std::string revision = name.version;
		system += " " + revision.substr(0, revision.find(' ')) + ", "
			+ name.machine;
	}
	std::string url = haikupinyin::ReportUrl("Haiku", HP_VERSION, system,
		fOptions);
	const char* argv[] = { url.c_str(), NULL };
	be_roster->Launch("application/x-vnd.Be.URL.https", 1, argv);
}
