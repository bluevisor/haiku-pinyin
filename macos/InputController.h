// 海酷拼音 Haiku Pinyin for macOS: an InputMethodKit input method around the
// portable engine.
#pragma once

#import <InputMethodKit/InputMethodKit.h>

// One per text client. They share one haikupinyin::Engine (the dictionaries
// are large), which is reset whenever another client takes it.
@interface HPInputController : IMKInputController
@end

// The input modes, 中 and 英: their icons are what the menu bar shows. Only
// 中 is listed (in Settings, the input menu); the toggle selects 英.
#define HPChineseMode @"com.bluevisor.inputmethod.HaikuPinyin.Chinese"
#define HPEnglishMode @"com.bluevisor.inputmethod.HaikuPinyin.English"

// english, simplified or traditional, by the system's language as the input
// menu has it (Strings.swift's L() chooses the same way).
NSString* HPLocalized(NSString* english, NSString* simplified,
	NSString* traditional);

// Loads the dictionaries and makes the candidate window, before the run loop.
void HPSetUp(IMKServer* server);
// Saves what it has learned; at exit.
void HPSave();

// Settings.swift
extern "C" void HPShowSettings(void);
// Updater.swift
extern "C" void HPUpdaterCheck(void);
// Whether a word is being typed: an update waits for it to end.
extern "C" bool HPComposing(void);
extern "C" void HPShowModeBubble(bool chinese, double x, double y);

// Candidates.swift. candidates: the page, joined with newlines.
typedef void (*HPIndexHandler)(int32_t);
extern "C" void HPSetCandidateHandlers(HPIndexHandler pick, HPIndexHandler page);
// ai: the candidate the AI moved first, or -1. keys: what picks each.
extern "C" void HPShowCandidates(const char* candidates, int32_t highlight,
	int32_t cloud, int32_t ai, bool hasPrevious, bool hasNext, double x,
	double y, double lineHeight, const char* keys);
extern "C" void HPHideCandidates(void);
// The AI's completion, gray after the cursor: the line's bottom left and
// height, and the client's font ("" and 0 when it doesn't say).
extern "C" void HPShowGhost(const char* text, double x, double y,
	double lineHeight, const char* fontName, double fontSize);
extern "C" void HPHideGhost(void);
