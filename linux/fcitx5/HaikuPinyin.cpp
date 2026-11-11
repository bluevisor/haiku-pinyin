// 海酷拼音 Haiku Pinyin for Linux: the Fcitx5 addon.

#include "HaikuPinyin.h"

#include <fcitx-utils/key.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputpanel.h>
#include <fcitx/statusarea.h>
#include <fcitx/text.h>
#include <fcitx/userinterfacemanager.h>

#include <memory>
#include <vector>

#include "../Common.h"

using haikupinyin::Key;

static const auto kShiftTapLimit = std::chrono::milliseconds(400);

// LayoutChoice's engine names.
static const char* const kLayouts[] = {
	"full", "pyjj", "xiaohe", "ziranma", "mspy", "abc",
};

static const unsigned kFuzzyInitials = haikupinyin::kFuzzyNL
	| haikupinyin::kFuzzyFH | haikupinyin::kFuzzyRL;


static void
ConfigFromOptions(const haikupinyin::Options& options, HaikuPinyinConfig* config)
{
	LayoutChoice layout = LayoutChoice::kFull;
	for (size_t i = 0; i < sizeof(kLayouts) / sizeof(kLayouts[0]); i++) {
		if (options.layout == kLayouts[i])
			layout = LayoutChoice(i);
	}
	config->layout.setValue(layout);
	config->vowels.setValue(VowelsChoice(options.vowels));
	config->script.setValue(options.script == haikupinyin::Script::kSimplified
		? ScriptChoice::kSimplified : ScriptChoice::kTraditional);
	config->pageSize.setValue(options.pageSize);
	config->fullwidth.setValue(options.fullwidthPunctuation);
	config->emoji.setValue(options.emoji);
	config->english.setValue(options.english);
	config->cloud.setValue(options.cloud);
	config->fuzzyRetroflex.setValue(
		(options.fuzzy & haikupinyin::kFuzzyRetroflex) != 0);
	config->fuzzyNasal.setValue((options.fuzzy & haikupinyin::kFuzzyNasal) != 0);
	config->fuzzyInitials.setValue(
		(options.fuzzy & kFuzzyInitials) == kFuzzyInitials);
}


static haikupinyin::Options
OptionsFromConfig(const HaikuPinyinConfig& config, haikupinyin::Options options)
{
	options.layout = kLayouts[int(*config.layout)];
	options.vowels = haikupinyin::Vowels(int(*config.vowels));
	options.script = *config.script == ScriptChoice::kSimplified
		? haikupinyin::Script::kSimplified : haikupinyin::Script::kTraditional;
	options.pageSize = *config.pageSize;
	options.fullwidthPunctuation = *config.fullwidth;
	options.emoji = *config.emoji;
	options.english = *config.english;
	options.cloud = *config.cloud;
	options.fuzzy = 0;
	if (*config.fuzzyRetroflex)
		options.fuzzy |= haikupinyin::kFuzzyRetroflex;
	if (*config.fuzzyNasal)
		options.fuzzy |= haikupinyin::kFuzzyNasal;
	if (*config.fuzzyInitials)
		options.fuzzy |= kFuzzyInitials;
	return options;
}


// #pragma mark - the candidate window


class Candidate : public fcitx::CandidateWord {
public:
	Candidate(HaikuPinyinEngine* engine, int index, const std::string& text)
		:
		fcitx::CandidateWord(fcitx::Text(text)),
		fEngine(engine),
		fIndex(index)
	{
	}

	void select(fcitx::InputContext* context) const override
	{
		fEngine->Select(context, fIndex);
	}

private:
	HaikuPinyinEngine* fEngine;
	int fIndex;
};


// The engine pages by itself; the window only ever has this page, and its
// arrows and scroll wheel come back to the engine.
class CandidatePage : public fcitx::CandidateList,
	public fcitx::PageableCandidateList,
	public fcitx::CursorMovableCandidateList {
public:
	CandidatePage(HaikuPinyinEngine* engine, fcitx::InputContext* context,
		const haikupinyin::Engine& pinyin)
		:
		fEngine(engine),
		fContext(context),
		fCursor(pinyin.Highlighted()),
		fPrevious(pinyin.HasPreviousPage()),
		fNext(pinyin.HasNextPage())
	{
		setPageable(this);
		setCursorMovable(this);
		std::vector<std::string> page = pinyin.PageCandidates();
		int cloud = pinyin.CloudIndexOnPage();
		// Digits, or letters while v-mode takes digits as typing.
		std::string keys = pinyin.PickKeys();
		for (size_t i = 0; i < page.size(); i++) {
			std::string text = page[i];
			if (int(i) == cloud)
				text += " ☁";
			fWords.push_back(std::make_unique<Candidate>(engine, int(i), text));
			fLabels.emplace_back(i < keys.size()
				? std::string(1, keys[i]) + "." : std::string());
		}
		if (fCursor >= int(page.size()))
			fCursor = -1;
	}

	const fcitx::Text& label(int index) const override { return fLabels[index]; }
	const fcitx::CandidateWord& candidate(int index) const override
	{
		return *fWords[index];
	}
	int size() const override { return int(fWords.size()); }
	int cursorIndex() const override { return fCursor; }
	fcitx::CandidateLayoutHint layoutHint() const override
	{
		return fcitx::CandidateLayoutHint::Horizontal;
	}

	bool hasPrev() const override { return fPrevious; }
	bool hasNext() const override { return fNext; }
	void prev() override { fEngine->PanelKey(fContext, Key::kPageUp); }
	void next() override { fEngine->PanelKey(fContext, Key::kPageDown); }
	bool usedNextBefore() const override { return fPrevious; }

	void prevCandidate() override { fEngine->PanelKey(fContext, Key::kLeft); }
	void nextCandidate() override { fEngine->PanelKey(fContext, Key::kRight); }

private:
	HaikuPinyinEngine* fEngine;
	fcitx::InputContext* fContext;
	std::vector<std::unique_ptr<Candidate>> fWords;
	std::vector<fcitx::Text> fLabels;
	int fCursor;
	bool fPrevious;
	bool fNext;
};


// #pragma mark - 中 / 英


std::string
ModeAction::shortText(fcitx::InputContext*) const
{
	return fEngine->Chinese() ? "中" : "英";
}


std::string
ModeAction::longText(fcitx::InputContext*) const
{
	return fEngine->Chinese() ? "中文 (Shift 切换)" : "English (Shift 切换)";
}


std::string
ModeAction::icon(fcitx::InputContext*) const
{
	return fEngine->Chinese() ? "haiku-pinyin" : "input-keyboard";
}


void
ModeAction::activate(fcitx::InputContext* context)
{
	fEngine->ToggleChinese(context);
}


// #pragma mark - the engine


HaikuPinyinEngine::HaikuPinyinEngine(fcitx::Instance* instance)
	:
	fInstance(instance),
	fModeAction(this),
	fWords([this](std::function<void()> work) {
		fDispatcher.schedule(std::move(work));
	}, [this]() -> haikupinyin::Engine* {
		if (!_EnsureLoaded())
			return nullptr;
		if (fcitx::InputContext* context = fLast.get())
			_Cancel(context);
		return &fEngine;
	})
{
	ReadSettings(&fSavedOptions, &fSavedChinese);
	ConfigFromOptions(fSavedOptions, &fConfig);
	fcitx::UserInterfaceManager& ui = fInstance->userInterfaceManager();
	ui.registerAction("haiku-pinyin-mode", &fModeAction);
	for (const WordsItem& item : kWordsItems) {
		auto action = std::make_unique<fcitx::SimpleAction>();
		action->setShortText(item.label);
		std::string key = item.key;
		action->connect<fcitx::SimpleAction::Activated>(
			[this, key](fcitx::InputContext*) { fWords.Activate(key); });
		ui.registerAction("haiku-pinyin-" + key, action.get());
		fWordsMenu.addAction(action.get());
		fWordsItems.push_back(std::move(action));
	}
	fWordsAction.setShortText(kWordsLabel);
	fWordsAction.setLongText("词库：自定义短语、导入、导出");
	fWordsAction.setIcon("accessories-dictionary");
	fWordsAction.setMenu(&fWordsMenu);
	ui.registerAction("haiku-pinyin-words", &fWordsAction);
	fDispatcher.attach(&fInstance->eventLoop());
	// Load the dictionaries now rather than on the first key.
	_EnsureLoaded();
}


HaikuPinyinEngine::~HaikuPinyinEngine()
{
	if (fLoaded)
		fEngine.SaveUserDictionary();
}


bool
HaikuPinyinEngine::_EnsureLoaded()
{
	if (fLoaded || fLoadFailed)
		return fLoaded;
	std::string error;
	if (!LoadData(&fEngine, &error)) {
		FCITX_ERROR() << "Haiku Pinyin: " << error;
		fLoadFailed = true;
		return false;
	}
	fLoaded = true;
	// Only now can the layout the settings name be checked.
	fEngine.SetOptions(fSavedOptions);
	fEngine.SetChinese(fSavedChinese);
	return true;
}


bool
HaikuPinyinEngine::Chinese() const
{
	return fLoaded ? fEngine.Chinese() : fSavedChinese;
}


// Empties the inline text and the candidate window.
void
HaikuPinyinEngine::_Clear(fcitx::InputContext* context)
{
	context->inputPanel().reset();
	context->updatePreedit();
	context->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}


// The shared engine now serves this context.
void
HaikuPinyinEngine::_Claim(fcitx::InputContext* context)
{
	fcitx::InputContext* last = fLast.get();
	if (last == context)
		return;
	if (last != nullptr)
		_Clear(last);
	if (fLoaded) {
		fEngine.Reset();
		fEngine.ClearContext();
	}
	fLast = context->watch();
}


void
HaikuPinyinEngine::_AskCloud()
{
	fCloud.Ask(fEngine, [this](const std::string& query,
			const std::string& text) {
		fDispatcher.schedule([this, query, text]() {
			fCloud.Answered();
			if (!fLoaded)
				return;
			fcitx::InputContext* context = fLast.get();
			if (!fEngine.SetCloudResult(query, text))
				_AskCloud();	// typing moved on while it was asked
			else if (context != nullptr)
				_ShowCandidates(context);
		});
	});
}


void
HaikuPinyinEngine::_ShowCandidates(fcitx::InputContext* context)
{
	fcitx::InputPanel& panel = context->inputPanel();
	if (!fEngine.Composing()) {
		panel.setCandidateList(nullptr);
		panel.setAuxUp(fcitx::Text());
	} else {
		_AskCloud();
		panel.setCandidateList(std::make_unique<CandidatePage>(this, context,
			fEngine));
		// The spelled pinyin, over the candidates, when the composition is
		// inline; otherwise the window shows the composition itself.
		if (context->capabilityFlags().test(fcitx::CapabilityFlag::Preedit))
			panel.setAuxUp(fcitx::Text(fEngine.Spelled()));
		else
			panel.setAuxUp(fcitx::Text());
	}
	context->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}


// Turns the engine's state into Fcitx5 calls: what to commit, the inline
// composition, the candidates.
void
HaikuPinyinEngine::_Sync(fcitx::InputContext* context)
{
	std::string commit = fEngine.TakeCommit();
	if (!commit.empty())
		context->commitString(commit);

	fcitx::InputPanel& panel = context->inputPanel();
	fcitx::Text preedit;
	if (fEngine.Composing()) {
		std::string text = fEngine.Preedit();
		size_t picked = fEngine.PickedLength();
		// The picked text and the pinyin still being typed: two clauses.
		if (picked > 0) {
			preedit.append(text.substr(0, picked), fcitx::TextFormatFlags{
				fcitx::TextFormatFlag::Underline,
				fcitx::TextFormatFlag::HighLight});
		}
		preedit.append(text.substr(picked), fcitx::TextFormatFlag::Underline);
		preedit.setCursor(int(text.size()));
	}
	if (context->capabilityFlags().test(fcitx::CapabilityFlag::Preedit)) {
		panel.setClientPreedit(preedit);
		panel.setPreedit(fcitx::Text());
	} else {
		panel.setClientPreedit(fcitx::Text());
		panel.setPreedit(preedit);
	}
	context->updatePreedit();
	_ShowCandidates(context);
}


void
HaikuPinyinEngine::_Cancel(fcitx::InputContext* context)
{
	fEngine.Reset();
	_Sync(context);
}


static void
MapKey(const fcitx::Key& raw, Key* key, char32_t* character,
	uint32_t* modifiers)
{
	fcitx::KeyStates states = raw.states();
	*modifiers = 0;
	if (states.test(fcitx::KeyState::Shift))
		*modifiers |= haikupinyin::kShift;
	if (states.test(fcitx::KeyState::Ctrl))
		*modifiers |= haikupinyin::kControl;
	if (states.testAny(fcitx::KeyStates{fcitx::KeyState::Super,
			fcitx::KeyState::Super2, fcitx::KeyState::Meta}))
		*modifiers |= haikupinyin::kCommand;
	if (states.test(fcitx::KeyState::Alt))
		*modifiers |= haikupinyin::kOption;

	*character = 0;
	switch (raw.sym()) {
		case FcitxKey_BackSpace: *key = Key::kBackspace; return;
		case FcitxKey_Return:
		case FcitxKey_KP_Enter: *key = Key::kReturn; return;
		case FcitxKey_space:
		case FcitxKey_KP_Space: *key = Key::kSpace; return;
		case FcitxKey_Escape: *key = Key::kEscape; return;
		case FcitxKey_Tab:
		case FcitxKey_KP_Tab: *key = Key::kTab; return;
		case FcitxKey_ISO_Left_Tab:
			*key = Key::kTab;
			*modifiers |= haikupinyin::kShift;
			return;
		case FcitxKey_Left:
		case FcitxKey_KP_Left: *key = Key::kLeft; return;
		case FcitxKey_Right:
		case FcitxKey_KP_Right: *key = Key::kRight; return;
		case FcitxKey_Up:
		case FcitxKey_KP_Up: *key = Key::kUp; return;
		case FcitxKey_Down:
		case FcitxKey_KP_Down: *key = Key::kDown; return;
		case FcitxKey_Page_Up:
		case FcitxKey_KP_Page_Up: *key = Key::kPageUp; return;
		case FcitxKey_Page_Down:
		case FcitxKey_KP_Page_Down: *key = Key::kPageDown; return;
		case FcitxKey_Home:
		case FcitxKey_KP_Home: *key = Key::kHome; return;
		case FcitxKey_End:
		case FcitxKey_KP_End: *key = Key::kEnd; return;
		case FcitxKey_Delete:
		case FcitxKey_KP_Delete: *key = Key::kDelete; return;
		default: break;
	}
	uint32_t unicode = fcitx::Key::keySymToUnicode(raw.sym());
	if (unicode >= 0x20 && unicode < 0x7f) {
		*key = Key::kCharacter;
		*character = unicode;
	} else if (unicode >= 0x80) {
		// A non-ASCII character: ends the word, then types itself.
		*key = Key::kCharacter;
		*character = 0x100;
	} else
		*key = Key::kOther;
}


void
HaikuPinyinEngine::ToggleChinese(fcitx::InputContext* context)
{
	if (!_EnsureLoaded())
		return;
	_Claim(context);
	fEngine.SetChinese(!fEngine.Chinese());
	_Sync(context);
	fModeAction.update(context);
	context->updateUserInterface(fcitx::UserInterfaceComponent::StatusArea);
	WriteSettings(fEngine.GetOptions(), fEngine.Chinese());
}


void
HaikuPinyinEngine::keyEvent(const fcitx::InputMethodEntry&,
	fcitx::KeyEvent& event)
{
	fcitx::InputContext* context = event.inputContext();
	const fcitx::Key& raw = event.rawKey();
	bool release = event.isRelease();
	_Claim(context);

	// Shift pressed and let go on its own switches 中/英.
	bool shift = raw.sym() == FcitxKey_Shift_L || raw.sym() == FcitxKey_Shift_R;
	fcitx::KeyStates others{fcitx::KeyState::Ctrl, fcitx::KeyState::Alt,
		fcitx::KeyState::Super, fcitx::KeyState::Super2, fcitx::KeyState::Meta};
	if (shift && !release) {
		fShiftAlone = !raw.states().testAny(others);
		fShiftDown = std::chrono::steady_clock::now();
		return;
	}
	if (shift && release) {
		bool tap = fShiftAlone
			&& std::chrono::steady_clock::now() - fShiftDown <= kShiftTapLimit;
		fShiftAlone = false;
		if (tap && _EnsureLoaded())
			ToggleChinese(context);
		return;
	}
	if (!release)
		fShiftAlone = false;

	if (release) {
		if (fSwallowed.erase(raw.code()) > 0)
			event.filterAndAccept();
		return;
	}
	if (raw.isModifier() || !_EnsureLoaded())
		return;

	// Caps Lock types English, as on the Mac.
	if (raw.states().test(fcitx::KeyState::CapsLock) && !fEngine.Composing())
		return;

	Key key;
	char32_t character;
	uint32_t modifiers;
	MapKey(raw, &key, &character, &modifiers);
	if (key == Key::kTab && (modifiers & haikupinyin::kShift) != 0)
		key = Key::kPageUp;

	bool used = fEngine.ProcessKey(key, character, modifiers);
	_Sync(context);
	if (used) {
		fSwallowed.insert(raw.code());
		event.filterAndAccept();
	}
}


void
HaikuPinyinEngine::activate(const fcitx::InputMethodEntry&,
	fcitx::InputContextEvent& event)
{
	fcitx::InputContext* context = event.inputContext();
	_EnsureLoaded();
	_Claim(context);
	context->statusArea().addAction(fcitx::StatusGroup::InputMethod,
		&fModeAction);
	context->statusArea().addAction(fcitx::StatusGroup::InputMethod,
		&fWordsAction);
}


void
HaikuPinyinEngine::deactivate(const fcitx::InputMethodEntry&,
	fcitx::InputContextEvent& event)
{
	if (!fLoaded)
		return;
	if (fLast.get() == event.inputContext())
		_Cancel(event.inputContext());
	fEngine.SaveUserDictionary();
}


// Focus moved, or the application moved the cursor or changed the text.
void
HaikuPinyinEngine::reset(const fcitx::InputMethodEntry&,
	fcitx::InputContextEvent& event)
{
	if (fLoaded && fLast.get() == event.inputContext()) {
		fEngine.Reset();
		fEngine.ClearContext();
		_Sync(event.inputContext());
	}
}


std::string
HaikuPinyinEngine::subMode(const fcitx::InputMethodEntry&, fcitx::InputContext&)
{
	return Chinese() ? "中文" : "English";
}


std::string
HaikuPinyinEngine::subModeLabelImpl(const fcitx::InputMethodEntry&,
	fcitx::InputContext&)
{
	return Chinese() ? "中" : "英";
}


void
HaikuPinyinEngine::Select(fcitx::InputContext* context, int index)
{
	if (!fLoaded || fLast.get() != context)
		return;
	fEngine.SelectOnPage(index);
	_Sync(context);
}


void
HaikuPinyinEngine::PanelKey(fcitx::InputContext* context, Key key)
{
	if (!fLoaded || fLast.get() != context)
		return;
	fEngine.ProcessKey(key, 0, 0);
	_ShowCandidates(context);
}


// New settings: cancel what's being typed, then use them.
void
HaikuPinyinEngine::_Apply(const haikupinyin::Options& options)
{
	if (!fLoaded) {
		fSavedOptions = options;
		ConfigFromOptions(options, &fConfig);
		return;
	}
	if (fcitx::InputContext* context = fLast.get())
		_Cancel(context);
	else
		fEngine.Reset();
	fEngine.SetOptions(options);
	// What the engine took: an unknown layout stays as it was.
	ConfigFromOptions(fEngine.GetOptions(), &fConfig);
}


void
HaikuPinyinEngine::setConfig(const fcitx::RawConfig& raw)
{
	fConfig.load(raw, true);
	haikupinyin::Options options = OptionsFromConfig(fConfig,
		fLoaded ? fEngine.GetOptions() : fSavedOptions);
	_Apply(options);
	WriteSettings(fLoaded ? fEngine.GetOptions() : fSavedOptions, Chinese());
}


// fcitx5-remote -r, or the settings file changed under it.
void
HaikuPinyinEngine::reloadConfig()
{
	haikupinyin::Options options;
	bool chinese = true;
	ReadSettings(&options, &chinese);
	_Apply(options);
	if (fLoaded)
		fEngine.SetChinese(chinese);
	else
		fSavedChinese = chinese;
}


void
HaikuPinyinEngine::save()
{
	if (fLoaded)
		fEngine.SaveUserDictionary();
}


fcitx::AddonInstance*
HaikuPinyinFactory::create(fcitx::AddonManager* manager)
{
	return new HaikuPinyinEngine(manager->instance());
}


FCITX_ADDON_FACTORY(HaikuPinyinFactory);
