// 海酷拼音 Haiku Pinyin for macOS: an InputMethodKit input method around the
// portable engine.
#pragma once

#import <InputMethodKit/InputMethodKit.h>

// One per text client. They share one haikupinyin::Engine (the dictionaries
// are large), which is reset whenever another client takes it.
@interface HPInputController : IMKInputController
@end

// Loads the dictionaries and makes the candidate window, before the run loop.
void HPSetUp(IMKServer* server);
// Saves what it has learned; at exit.
void HPSave();

// Settings.swift
extern "C" void HPShowSettings(void);
extern "C" void HPShowModeBubble(bool chinese, double x, double y);
