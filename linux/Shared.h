// 海酷拼音 Haiku Pinyin for Linux: what the IBus engine (main.cpp), its
// settings and menu (Settings.cpp) and the cloud (Cloud.cpp) share.
#pragma once

#include <ibus.h>

#include <string>

#include "../engine/Engine.h"

// One haikupinyin::Engine for every input context.
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

// Cloud.cpp: asks Google Input Tools about the composition, on the side.
void AskCloud();

// main.cpp
void ShowCandidates(IBusEngine* engine);
void Cancel(IBusEngine* engine);
void ToggleChinese(IBusEngine* engine);
