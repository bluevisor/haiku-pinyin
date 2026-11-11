// 海酷拼音 Haiku Pinyin for Linux: the settings window, in GTK 3.
//   haiku-pinyin-setup
// IBus starts it as the engine's setup program (GNOME Settings, ibus-setup,
// 设置… in the top-bar menu). It reads and writes ~/.config/haiku-pinyin/
// settings, the file both front ends keep: the IBus engine watches it, and
// a running Fcitx5 is told to read it again. It watches the file too, so a
// change made in a menu shows here.

#include <gtk/gtk.h>
#include <sys/utsname.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "../../engine/Engine.h"
#include "../../engine/Phrases.h"
#include "../../engine/Report.h"
#include "../Common.h"

#ifndef HP_VERSION
#define HP_VERSION "0"
#endif

static bool sChinese = true;


// The menus' Chinese, or English when the locale isn't Chinese.
static const char*
T(const char* chinese, const char* english)
{
	return sChinese ? chinese : english;
}


static const struct {
	const char* layout;
	const char* chinese;
	const char* english;
} kLayouts[] = {
	{"full", "全拼", "Full pinyin"},
	{"pyjj", "双拼 · 拼音加加", "Shuangpin · Pinyin Jiajia"},
	{"xiaohe", "双拼 · 小鹤", "Shuangpin · Xiaohe"},
	{"ziranma", "双拼 · 自然码", "Shuangpin · Ziranma"},
	{"mspy", "双拼 · 微软", "Shuangpin · Microsoft"},
	{"abc", "双拼 · 智能ABC", "Shuangpin · Intelligent ABC"},
};

static const struct {
	unsigned mask;
	const char* label;
} kFuzzyGroups[] = {
	{haikupinyin::kFuzzyRetroflex, "z=zh  c=ch  s=sh"},
	{haikupinyin::kFuzzyNasal, "an=ang  en=eng  in=ing"},
	{haikupinyin::kFuzzyNL | haikupinyin::kFuzzyFH | haikupinyin::kFuzzyRL,
		"n=l  f=h  r=l"},
};


struct Setup {
	GtkApplication* application = nullptr;
	GtkWidget* window = nullptr;
	GtkWidget* layout = nullptr;
	GtkWidget* vowels = nullptr;
	GtkWidget* vowelsRow = nullptr;
	GtkWidget* script = nullptr;
	GtkWidget* pageSize = nullptr;
	GtkWidget* fullwidth = nullptr;
	GtkWidget* emoji = nullptr;
	GtkWidget* english = nullptr;
	GtkWidget* cloud = nullptr;
	GtkWidget* fuzzy[3] = {};
	GFileMonitor* monitor = nullptr;
	bool showing = false;	// setting the controls, not the user
	// Loaded for import and export, the first time.
	std::unique_ptr<haikupinyin::Engine> engine;
};


static std::string
SettingsPath()
{
	return ConfigDirectory() + "/settings";
}


static bool
Fcitx5Running()
{
	GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
	if (bus == nullptr)
		return false;
	GVariant* reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus",
		"/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner",
		g_variant_new("(s)", "org.fcitx.Fcitx5"), G_VARIANT_TYPE("(b)"),
		G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
	gboolean owned = FALSE;
	if (reply != nullptr) {
		g_variant_get(reply, "(b)", &owned);
		g_variant_unref(reply);
	}
	g_object_unref(bus);
	return owned;
}


// Fcitx5 reads its settings when it starts and when told to; IBus watches
// the file itself.
static void
TellFcitx5()
{
	GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
	if (bus == nullptr)
		return;
	g_dbus_connection_call(bus, "org.fcitx.Fcitx5", "/controller",
		"org.fcitx.Fcitx.Controller1", "ReloadAddonConfig",
		g_variant_new("(s)", "haiku-pinyin"), nullptr,
		G_DBUS_CALL_FLAGS_NO_AUTO_START, 2000, nullptr, nullptr, nullptr);
	g_object_unref(bus);
}


static void
Tell(Setup* setup, const std::string& text, GtkMessageType type = GTK_MESSAGE_INFO)
{
	GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(setup->window),
		GtkDialogFlags(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), type,
		GTK_BUTTONS_OK,
		"%s", text.c_str());
	g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy), nullptr);
	gtk_widget_show(dialog);
}


// The file into the controls.
static void
Show(Setup* setup)
{
	haikupinyin::Options options;
	bool chinese = true;
	ReadSettings(&options, &chinese);
	setup->showing = true;
	if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(setup->layout),
			options.layout.c_str())) {
		gtk_combo_box_set_active(GTK_COMBO_BOX(setup->layout), 0);
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(setup->vowels), int(options.vowels));
	gtk_widget_set_sensitive(setup->vowelsRow, options.layout != "full");
	gtk_combo_box_set_active(GTK_COMBO_BOX(setup->script),
		options.script == haikupinyin::Script::kSimplified ? 0 : 1);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(setup->pageSize), options.pageSize);
	gtk_switch_set_active(GTK_SWITCH(setup->fullwidth),
		options.fullwidthPunctuation);
	gtk_switch_set_active(GTK_SWITCH(setup->emoji), options.emoji);
	gtk_switch_set_active(GTK_SWITCH(setup->english), options.english);
	gtk_switch_set_active(GTK_SWITCH(setup->cloud), options.cloud);
	for (int i = 0; i < 3; i++) {
		unsigned mask = kFuzzyGroups[i].mask;
		gtk_switch_set_active(GTK_SWITCH(setup->fuzzy[i]),
			(options.fuzzy & mask) == mask);
	}
	setup->showing = false;
}


// The controls into the file. What the window doesn't show (中/英) stays as
// the file has it.
static void
Save(Setup* setup)
{
	if (setup->showing)
		return;
	haikupinyin::Options options;
	bool chinese = true;
	ReadSettings(&options, &chinese);
	if (const gchar* layout = gtk_combo_box_get_active_id(
			GTK_COMBO_BOX(setup->layout))) {
		options.layout = layout;
	}
	options.vowels = haikupinyin::Vowels(std::max(0,
		gtk_combo_box_get_active(GTK_COMBO_BOX(setup->vowels))));
	options.script = gtk_combo_box_get_active(GTK_COMBO_BOX(setup->script)) == 1
		? haikupinyin::Script::kTraditional : haikupinyin::Script::kSimplified;
	options.pageSize = gtk_spin_button_get_value_as_int(
		GTK_SPIN_BUTTON(setup->pageSize));
	options.fullwidthPunctuation = gtk_switch_get_active(
		GTK_SWITCH(setup->fullwidth));
	options.emoji = gtk_switch_get_active(GTK_SWITCH(setup->emoji));
	options.english = gtk_switch_get_active(GTK_SWITCH(setup->english));
	options.cloud = gtk_switch_get_active(GTK_SWITCH(setup->cloud));
	for (int i = 0; i < 3; i++) {
		unsigned mask = kFuzzyGroups[i].mask;
		if (gtk_switch_get_active(GTK_SWITCH(setup->fuzzy[i])))
			options.fuzzy |= mask;
		else
			options.fuzzy &= ~mask;
	}
	gtk_widget_set_sensitive(setup->vowelsRow, options.layout != "full");
	if (!WriteSettings(options, chinese)) {
		Tell(setup, std::string(T("没能保存设置：", "Couldn't save the settings: "))
			+ SettingsPath(), GTK_MESSAGE_ERROR);
		return;
	}
	TellFcitx5();
}


static void
Changed(GtkWidget*, gpointer data)
{
	Save(static_cast<Setup*>(data));
}


static void
SwitchChanged(GObject*, GParamSpec*, gpointer data)
{
	Save(static_cast<Setup*>(data));
}


static void
FileChanged(GFileMonitor*, GFile*, GFile*, GFileMonitorEvent event,
	gpointer data)
{
	if (event != G_FILE_MONITOR_EVENT_DELETED)
		Show(static_cast<Setup*>(data));
}


// #pragma mark - 词库


static void
Open(Setup* setup, const std::string& path)
{
	gchar* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
	GError* error = nullptr;
	if (uri == nullptr || !g_app_info_launch_default_for_uri(uri, nullptr, &error)) {
		Tell(setup, std::string(T("打不开 ", "Can't open ")) + path
			+ (error != nullptr ? std::string("\n") + error->message : ""),
			GTK_MESSAGE_ERROR);
	}
	g_clear_error(&error);
	g_free(uri);
}


static void
EditPhrases(GtkWidget*, gpointer data)
{
	haikupinyin::Phrases phrases;
	phrases.SetPath(PhrasesPath());
	phrases.CreateFile();
	Open(static_cast<Setup*>(data), PhrasesPath());
}


static void
OpenFolder(GtkWidget*, gpointer data)
{
	Open(static_cast<Setup*>(data), ConfigDirectory());
}


// The engine, with the dictionaries, for import and export. The running
// input method reads the learned words again when the file changes.
static haikupinyin::Engine*
Loaded(Setup* setup)
{
	if (setup->engine)
		return setup->engine.get();
	GdkWindow* window = gtk_widget_get_window(setup->window);
	GdkCursor* busy = gdk_cursor_new_from_name(gdk_window_get_display(window),
		"wait");
	gdk_window_set_cursor(window, busy);
	while (gtk_events_pending())
		gtk_main_iteration();
	auto engine = std::make_unique<haikupinyin::Engine>();
	std::string error;
	bool loaded = LoadData(engine.get(), &error);
	gdk_window_set_cursor(window, nullptr);
	g_clear_object(&busy);
	if (!loaded) {
		Tell(setup, std::string(T("没能载入词典：", "Couldn't load the dictionaries: "))
			+ error, GTK_MESSAGE_ERROR);
		return nullptr;
	}
	setup->engine = std::move(engine);
	return setup->engine.get();
}


// A file to import from, or to export to; "" when cancelled.
static std::string
Choose(Setup* setup, bool save)
{
	GtkFileChooserNative* chooser = gtk_file_chooser_native_new(save
		? T("导出学到的词", "Export Learned Words")
		: T("导入词库：海酷拼音、Rime、搜狗，或 Mac 的文本替换",
			"Import Words: from Haiku Pinyin, Rime, Sogou, or the Mac's text replacements"),
		GTK_WINDOW(setup->window),
		save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,
		save ? T("导出", "Export") : T("导入", "Import"), T("取消", "Cancel"));
	GtkFileChooser* files = GTK_FILE_CHOOSER(chooser);
	gtk_file_chooser_set_current_folder(files, g_get_home_dir());
	if (save) {
		gtk_file_chooser_set_do_overwrite_confirmation(files, TRUE);
		gtk_file_chooser_set_current_name(files,
			T("海酷拼音词库.txt", "Haiku Pinyin words.txt"));
	}
	std::string path;
	if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
		gchar* chosen = gtk_file_chooser_get_filename(files);
		if (chosen != nullptr)
			path = chosen;
		g_free(chosen);
	}
	g_object_unref(chooser);
	return path;
}


static void
Import(GtkWidget*, gpointer data)
{
	Setup* setup = static_cast<Setup*>(data);
	std::string path = Choose(setup, false);
	if (path.empty())
		return;
	if (haikupinyin::Engine* engine = Loaded(setup))
		Tell(setup, engine->ImportWords(path).Summary());
}


static void
Export(GtkWidget*, gpointer data)
{
	Setup* setup = static_cast<Setup*>(data);
	std::string path = Choose(setup, true);
	if (path.empty())
		return;
	haikupinyin::Engine* engine = Loaded(setup);
	if (engine == nullptr)
		return;
	int count = 0;
	if (engine->ExportWords(path, &count)) {
		Tell(setup, sChinese
			? "导出了 " + std::to_string(count) + " 个学到的词：" + path
			: "Exported " + std::to_string(count) + " learned words: " + path);
	} else {
		Tell(setup, std::string(T("没能写入 ", "Couldn't write ")) + path,
			GTK_MESSAGE_ERROR);
	}
}


// #pragma mark - reporting a problem


// "Ubuntu 24.04.1 LTS, GNOME (wayland), aarch64".
static std::string
SystemDescription()
{
	std::string system;
	if (gchar* name = g_get_os_info(G_OS_INFO_KEY_PRETTY_NAME)) {
		system = name;
		g_free(name);
	} else
		system = "Linux";
	if (const char* desktop = getenv("XDG_CURRENT_DESKTOP"))
		system += std::string(", ") + desktop;
	if (const char* session = getenv("XDG_SESSION_TYPE"))
		system += std::string(" (") + session + ")";
	struct utsname machine;
	if (uname(&machine) == 0)
		system += std::string(", ") + machine.machine;
	return system;
}


// A new GitHub issue with the version, the system and the settings filled
// in; never anything typed or learned.
static void
Report(GtkWidget*, gpointer data)
{
	Setup* setup = static_cast<Setup*>(data);
	haikupinyin::Options options;
	bool chinese = true;
	ReadSettings(&options, &chinese);
	std::string url = haikupinyin::ReportUrl(
		Fcitx5Running() ? "Linux, Fcitx5" : "Linux, IBus", HP_VERSION,
		SystemDescription(), options);
	GError* error = nullptr;
	if (!gtk_show_uri_on_window(GTK_WINDOW(setup->window), url.c_str(),
			GDK_CURRENT_TIME, &error)) {
		g_clear_error(&error);
		// No handler GIO knows: xdg-open may.
		const gchar* argv[] = {"xdg-open", url.c_str(), nullptr};
		if (!g_spawn_async(nullptr, const_cast<gchar**>(argv), nullptr,
				G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, &error)) {
			Tell(setup, std::string(T("打不开浏览器。链接：\n",
				"Can't open a browser. The link:\n")) + url, GTK_MESSAGE_ERROR);
		}
		g_clear_error(&error);
	}
}


// #pragma mark - the window


static GtkWidget*
Heading(const char* text)
{
	GtkWidget* label = gtk_label_new(nullptr);
	gchar* markup = g_markup_printf_escaped("<b>%s</b>", text);
	gtk_label_set_markup(GTK_LABEL(label), markup);
	g_free(markup);
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_widget_set_margin_top(label, 18);
	gtk_widget_set_margin_bottom(label, 6);
	return label;
}


// A boxed list, as GNOME Settings has them.
static GtkWidget*
Section(GtkWidget* page, const char* heading)
{
	gtk_box_pack_start(GTK_BOX(page), Heading(heading), FALSE, FALSE, 0);
	GtkWidget* list = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
	GtkWidget* frame = gtk_frame_new(nullptr);
	gtk_container_add(GTK_CONTAINER(frame), list);
	gtk_box_pack_start(GTK_BOX(page), frame, FALSE, FALSE, 0);
	return list;
}


// A row: the title (and a note under it), the control on the right.
static GtkWidget*
Row(GtkWidget* list, const char* title, GtkWidget* control,
	const char* note = nullptr)
{
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_widget_set_margin_start(box, 12);
	gtk_widget_set_margin_end(box, 12);
	gtk_widget_set_margin_top(box, 8);
	gtk_widget_set_margin_bottom(box, 8);
	GtkWidget* text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_widget_set_valign(text, GTK_ALIGN_CENTER);
	GtkWidget* label = gtk_label_new(title);
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_box_pack_start(GTK_BOX(text), label, FALSE, FALSE, 0);
	if (note != nullptr) {
		GtkWidget* small = gtk_label_new(note);
		gtk_label_set_xalign(GTK_LABEL(small), 0);
		gtk_label_set_line_wrap(GTK_LABEL(small), TRUE);
		gtk_label_set_max_width_chars(GTK_LABEL(small), 44);
		gtk_style_context_add_class(gtk_widget_get_style_context(small),
			"dim-label");
		PangoAttrList* smaller = pango_attr_list_new();
		pango_attr_list_insert(smaller, pango_attr_scale_new(PANGO_SCALE_SMALL));
		gtk_label_set_attributes(GTK_LABEL(small), smaller);
		pango_attr_list_unref(smaller);
		gtk_box_pack_start(GTK_BOX(text), small, FALSE, FALSE, 0);
	}
	gtk_box_pack_start(GTK_BOX(box), text, TRUE, TRUE, 0);
	if (control != nullptr) {
		gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
		gtk_box_pack_end(GTK_BOX(box), control, FALSE, FALSE, 0);
	}
	GtkWidget* row = gtk_list_box_row_new();
	gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
	gtk_container_add(GTK_CONTAINER(row), box);
	gtk_container_add(GTK_CONTAINER(list), row);
	return row;
}


static GtkWidget*
Switch(Setup* setup)
{
	GtkWidget* toggle = gtk_switch_new();
	g_signal_connect(toggle, "notify::active", G_CALLBACK(SwitchChanged), setup);
	return toggle;
}


static GtkWidget*
Button(const char* label, GCallback clicked, Setup* setup)
{
	GtkWidget* button = gtk_button_new_with_label(label);
	g_signal_connect(button, "clicked", clicked, setup);
	return button;
}


// A page of sections, scrolling when the screen is short.
static void
Page(GtkWidget* stack, const char* name, const char* title, GtkWidget** page)
{
	*page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_margin_start(*page, 24);
	gtk_widget_set_margin_end(*page, 24);
	gtk_widget_set_margin_bottom(*page, 24);
	GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
		GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll),
		TRUE);
	gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 760);
	gtk_container_add(GTK_CONTAINER(scroll), *page);
	gtk_stack_add_titled(GTK_STACK(stack), scroll, name, title);
}


static void
Build(Setup* setup)
{
	GtkWidget* window = gtk_application_window_new(setup->application);
	setup->window = window;
	gtk_window_set_title(GTK_WINDOW(window),
		T("海酷拼音设置", "Haiku Pinyin Settings"));
	gtk_window_set_icon_name(GTK_WINDOW(window), "haiku-pinyin");
	gtk_window_set_default_size(GTK_WINDOW(window), 560, -1);

	// The options, and the words and the problem report, as two pages.
	GtkWidget* stack = gtk_stack_new();
	gtk_stack_set_vhomogeneous(GTK_STACK(stack), FALSE);
	gtk_stack_set_transition_type(GTK_STACK(stack),
		GTK_STACK_TRANSITION_TYPE_CROSSFADE);
	GtkWidget* switcher = gtk_stack_switcher_new();
	gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));
	GtkWidget* bar = gtk_header_bar_new();
	// The window takes its title from the bar's, even under a custom one.
	gtk_header_bar_set_title(GTK_HEADER_BAR(bar),
		T("海酷拼音设置", "Haiku Pinyin Settings"));
	gtk_header_bar_set_custom_title(GTK_HEADER_BAR(bar), switcher);
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(bar), TRUE);
	gtk_window_set_titlebar(GTK_WINDOW(window), bar);

	GtkWidget* page;
	Page(stack, "options", T("选项", "Options"), &page);
	GtkWidget* list = Section(page, T("输入", "Input"));
	setup->layout = gtk_combo_box_text_new();
	for (const auto& layout : kLayouts) {
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(setup->layout),
			layout.layout, T(layout.chinese, layout.english));
	}
	g_signal_connect(setup->layout, "changed", G_CALLBACK(Changed), setup);
	Row(list, T("全拼 / 双拼", "Typing scheme"), setup->layout);

	// In the order of haikupinyin::Vowels.
	setup->vowels = gtk_combo_box_text_new();
	for (const char* label : {"aa oo ee", "oa oo oe",
			T("a o e 单键", "a o e (one key)")}) {
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(setup->vowels), label);
	}
	g_signal_connect(setup->vowels, "changed", G_CALLBACK(Changed), setup);
	setup->vowelsRow = Row(list, T("双拼 a o e", "Shuangpin a o e"),
		setup->vowels, T("双拼里单独的 a、o、e 怎么打",
			"How shuangpin types a, o and e alone"));

	setup->script = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(setup->script),
		T("简体中文", "Simplified Chinese"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(setup->script),
		T("繁體中文", "Traditional Chinese"));
	g_signal_connect(setup->script, "changed", G_CALLBACK(Changed), setup);
	Row(list, T("简体 / 繁體", "Characters"), setup->script);
	setup->fullwidth = Switch(setup);
	Row(list, T("全角标点", "Full-width punctuation"), setup->fullwidth);

	list = Section(page, T("候选", "Candidates"));
	setup->pageSize = gtk_spin_button_new_with_range(3, 10, 1);
	g_signal_connect(setup->pageSize, "value-changed", G_CALLBACK(Changed), setup);
	Row(list, T("每页候选数", "Candidates per page"), setup->pageSize);
	setup->emoji = Switch(setup);
	Row(list, T("Emoji 候选", "Emoji"), setup->emoji,
		T("太阳 也给出 🌞", "太阳 also offers 🌞"));
	setup->english = Switch(setup);
	Row(list, T("中英混输", "Mixed Chinese and English"), setup->english,
		T("hello 也给出 hello", "Typing hello also offers hello"));
	setup->cloud = Switch(setup);
	Row(list, T("云候选 (Google)", "Cloud candidates (Google)"), setup->cloud,
		T("把正在输入的拼音发给 Google 输入工具，不含已上屏的文字。",
			"Sends the pinyin being typed to Google Input Tools, never the text already entered."));

	list = Section(page, T("模糊音", "Fuzzy pinyin"));
	for (int i = 0; i < 3; i++) {
		setup->fuzzy[i] = Switch(setup);
		Row(list, kFuzzyGroups[i].label, setup->fuzzy[i]);
	}

	Page(stack, "words", T("词库", "Words"), &page);
	list = Section(page, T("词库", "Word lists"));
	Row(list, T("自定义短语", "Custom phrases"),
		Button(T("编辑…", "Edit…"), G_CALLBACK(EditPhrases), setup),
		T("打出编码时给出的短语，如 dz 出地址",
			"Offered when you type their code: dz for your address, say"));
	Row(list, T("导入词库", "Import words"),
		Button(T("导入…", "Import…"), G_CALLBACK(Import), setup),
		T("海酷拼音、Rime、搜狗的词库或自定义短语，或 Mac 的文本替换",
			"From Haiku Pinyin, Rime or Sogou, or the Mac's text replacements"));
	Row(list, T("导出学到的词", "Export learned words"),
		Button(T("导出…", "Export…"), G_CALLBACK(Export), setup),
		T("存成文本，可以再导入", "As text, to import again"));
	Row(list, T("词库文件夹", "Word list folder"),
		Button(T("打开", "Open"), G_CALLBACK(OpenFolder), setup),
		"~/.config/haiku-pinyin");

	list = Section(page, T("反馈", "Feedback"));
	Row(list, T("报告问题", "Report a problem"),
		Button(T("报告问题…", "Report a Problem…"), G_CALLBACK(Report), setup),
		T("在 GitHub 上新建问题，附上版本、系统和设置，不含输入的文字。",
			"Opens a new GitHub issue with the version, the system and the settings, never what you type."));
	std::string version = std::string(T("版本 ", "Version ")) + HP_VERSION;
	Row(list, version.c_str(), nullptr);

	gtk_container_add(GTK_CONTAINER(window), stack);

	Show(setup);
	GFile* file = g_file_new_for_path(SettingsPath().c_str());
	setup->monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, nullptr,
		nullptr);
	g_object_unref(file);
	if (setup->monitor != nullptr) {
		g_signal_connect(setup->monitor, "changed", G_CALLBACK(FileChanged),
			setup);
	}
	gtk_widget_show_all(window);
}


static void
Activate(GApplication* application, gpointer data)
{
	Setup* setup = static_cast<Setup*>(data);
	if (setup->window == nullptr)
		Build(setup);
	gtk_window_present(GTK_WINDOW(setup->window));
}


int
main(int argc, char** argv)
{
	if (argc > 1 && strcmp(argv[1], "--version") == 0) {
		printf("haiku-pinyin-setup %s\n", HP_VERSION);
		return 0;
	}
	const gchar* const* languages = g_get_language_names();
	sChinese = languages[0] != nullptr && g_str_has_prefix(languages[0], "zh");

	Setup setup;
	// One window: opening it again brings this one forward.
	setup.application = gtk_application_new("io.github.bluevisor.HaikuPinyin.Setup",
		GApplicationFlags(0));
	g_signal_connect(setup.application, "activate", G_CALLBACK(Activate), &setup);
	int status = g_application_run(G_APPLICATION(setup.application), argc, argv);
	g_clear_object(&setup.monitor);
	g_object_unref(setup.application);
	return status;
}
