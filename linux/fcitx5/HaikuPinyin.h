// 海酷拼音 Haiku Pinyin for Linux: a Fcitx5 input method addon around the
// portable engine. It does what the IBus engine (../main.cpp) does, and
// keeps its settings and words in the same files (../Common.h).
#pragma once

#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/option.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/trackableobject.h>
#include <fcitx/action.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <fcitx/menu.h>

#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../../engine/Engine.h"
#include "../Cloud.h"
#include "../Words.h"


// The settings, as Fcitx5's configuration tool shows them. Saved in
// ~/.config/haiku-pinyin/settings, beside the IBus engine's, not in
// ~/.config/fcitx5. The names are the IBus menu's.
enum class LayoutChoice { kFull, kPyjj, kXiaohe, kZiranma, kMspy, kAbc };
FCITX_CONFIG_ENUM_NAME(LayoutChoice, "全拼", "双拼 · 拼音加加", "双拼 · 小鹤",
	"双拼 · 自然码", "双拼 · 微软", "双拼 · 智能ABC");

// In the order of haikupinyin::Vowels.
enum class VowelsChoice { kDoubled, kOLed, kSingle };
FCITX_CONFIG_ENUM_NAME(VowelsChoice, "aa oo ee", "oa oo oe", "a o e 单键");

enum class ScriptChoice { kSimplified, kTraditional };
FCITX_CONFIG_ENUM_NAME(ScriptChoice, "简体中文", "繁體中文");

// The defaults are the engine's.
FCITX_CONFIGURATION(HaikuPinyinConfig,
	fcitx::Option<LayoutChoice> layout{this, "Layout", "全拼 / 双拼",
		LayoutChoice::kFull};
	fcitx::Option<VowelsChoice> vowels{this, "Vowels", "双拼 a o e",
		VowelsChoice(haikupinyin::Options().vowels)};
	fcitx::Option<ScriptChoice> script{this, "Script", "简体 / 繁體",
		ScriptChoice::kSimplified};
	fcitx::Option<int, fcitx::IntConstrain> pageSize{this, "PageSize",
		"每页候选数", haikupinyin::Options().pageSize,
		fcitx::IntConstrain(3, 10)};
	fcitx::Option<bool> fullwidth{this, "Fullwidth", "全角标点",
		haikupinyin::Options().fullwidthPunctuation};
	fcitx::Option<bool> emoji{this, "Emoji", "Emoji 候选",
		haikupinyin::Options().emoji};
	fcitx::Option<bool> english{this, "English", "中英混输",
		haikupinyin::Options().english};
	fcitx::Option<bool> cloud{this, "Cloud", "云候选 (Google)",
		haikupinyin::Options().cloud};
	fcitx::Option<bool> fuzzyRetroflex{this, "FuzzyRetroflex",
		"模糊音  z=zh  c=ch  s=sh", false};
	fcitx::Option<bool> fuzzyNasal{this, "FuzzyNasal",
		"模糊音  an=ang  en=eng  in=ing", false};
	fcitx::Option<bool> fuzzyInitials{this, "FuzzyInitials",
		"模糊音  n=l  f=h  r=l", false};
);


class HaikuPinyinEngine;

// 中 / 英 in the status area and the tray menu.
class ModeAction : public fcitx::Action {
public:
	ModeAction(HaikuPinyinEngine* engine) : fEngine(engine) {}
	std::string shortText(fcitx::InputContext*) const override;
	std::string longText(fcitx::InputContext*) const override;
	std::string icon(fcitx::InputContext*) const override;
	void activate(fcitx::InputContext* context) override;

private:
	HaikuPinyinEngine* fEngine;
};


// One haikupinyin::Engine for every input context (the dictionaries are
// large), reset whenever another context types, as with IBus.
class HaikuPinyinEngine : public fcitx::InputMethodEngineV2 {
public:
	HaikuPinyinEngine(fcitx::Instance* instance);
	~HaikuPinyinEngine() override;

	void keyEvent(const fcitx::InputMethodEntry& entry,
		fcitx::KeyEvent& event) override;
	void activate(const fcitx::InputMethodEntry& entry,
		fcitx::InputContextEvent& event) override;
	void deactivate(const fcitx::InputMethodEntry& entry,
		fcitx::InputContextEvent& event) override;
	void reset(const fcitx::InputMethodEntry& entry,
		fcitx::InputContextEvent& event) override;
	std::string subMode(const fcitx::InputMethodEntry& entry,
		fcitx::InputContext& context) override;
	std::string subModeLabelImpl(const fcitx::InputMethodEntry& entry,
		fcitx::InputContext& context) override;

	const fcitx::Configuration* getConfig() const override { return &fConfig; }
	void setConfig(const fcitx::RawConfig& config) override;
	void reloadConfig() override;
	void save() override;

	bool Chinese() const;
	void ToggleChinese(fcitx::InputContext* context);
	// From the candidate window: a click, the page arrows, the scroll wheel.
	void Select(fcitx::InputContext* context, int index);
	void PanelKey(fcitx::InputContext* context, haikupinyin::Key key);

private:
	bool _EnsureLoaded();
	void _Apply(const haikupinyin::Options& options);
	void _Claim(fcitx::InputContext* context);
	void _Cancel(fcitx::InputContext* context);
	void _Sync(fcitx::InputContext* context);
	void _ShowCandidates(fcitx::InputContext* context);
	void _Clear(fcitx::InputContext* context);
	void _AskCloud();

	fcitx::Instance* fInstance;
	haikupinyin::Engine fEngine;
	haikupinyin::Options fSavedOptions;	// applied once the data loads
	bool fSavedChinese = true;
	bool fLoaded = false;
	bool fLoadFailed = false;
	HaikuPinyinConfig fConfig;
	ModeAction fModeAction;
	// The context the engine state is for.
	fcitx::TrackableObjectReference<fcitx::InputContext> fLast;
	std::set<int> fSwallowed;	// keycodes whose releases we eat
	bool fShiftAlone = false;
	std::chrono::steady_clock::time_point fShiftDown;
	fcitx::EventDispatcher fDispatcher;
	// After the dispatcher: their threads hand work to it, so they go first.
	Cloud fCloud;
	WordsMenu fWords;
	// 词库 in the status area: custom phrases, import, export.
	std::vector<std::unique_ptr<fcitx::SimpleAction>> fWordsItems;
	fcitx::Menu fWordsMenu;
	fcitx::SimpleAction fWordsAction;
};


class HaikuPinyinFactory : public fcitx::AddonFactory {
public:
	fcitx::AddonInstance* create(fcitx::AddonManager* manager) override;
};
