// 海酷拼音 Haiku Pinyin for Linux: what the IBus engine (main.cpp) and its
// settings and menu (Settings.cpp) share. What it shares with the Fcitx5
// addon is in Common.h and Cloud.h.
#pragma once

#include <ibus.h>

#include <string>

#include "../engine/Engine.h"
#include "Cloud.h"
#include "Common.h"

// One haikupinyin::Engine for every input context.
struct Shared {
	haikupinyin::Engine engine;
	haikupinyin::Options savedOptions;	// applied once the data loads
	bool savedChinese = true;
	bool loaded = false;
	bool loadFailed = false;
	IBusEngine* focused = nullptr;
	IBusEngine* last = nullptr;			// the context the engine state is for
	Cloud cloud;
};

extern Shared* sShared;

// Settings.cpp: ~/.config/haiku-pinyin/settings, the data, the menu.
void LoadSettings();
void SaveSettings();
// Loads the dictionaries the first time; false when there are none.
bool EnsureLoaded();
// Hands the panel the menu; changed: it was showing, update each item.
void UpdateProperties(IBusEngine* engine, bool changed = false);
// A menu item was chosen.
void PropertyActivate(IBusEngine* engine, const gchar* name, guint state);

// main.cpp
// Asks the cloud about the composition, on the side.
void AskCloud();
void ShowCandidates(IBusEngine* engine);
void Cancel(IBusEngine* engine);
void ToggleChinese(IBusEngine* engine);
