// 海酷拼音 Haiku Pinyin for macOS: the AI helper (macos/ai), a separate
// process that holds a small language model. It's started when the settings
// want it and its models are installed, and talked to in JSON lines. Answers
// come back on the main queue; a request a newer one of its kind overtook
// gets none.
#pragma once

#import <Foundation/Foundation.h>

typedef void (^HPRankHandler)(NSArray<NSNumber*>* scores);
typedef void (^HPCompleteHandler)(NSString* text);

// Where the models go: ~/Library/Application Support/HaikuPinyin/models, or
// $HAIKU_PINYIN_MODELS.
NSString* HPAIModelDirectory(void);
// Whether the helper and the model it would start (the one chosen, or the
// default or its backup) are there.
bool HPAIAvailable(void);
// Available, and the model didn't fail to load: the settings show the AI
// off when it isn't. HPAIStateChanged is posted when this may have changed.
extern "C" bool HPAIUsable(void);
// The model failed to load (a broken download, say).
extern "C" bool HPAIFailed(void);
extern NSString* const HPAIStateChanged;
// Starts, stops or restarts the helper to match the settings (aiPicks,
// aiComplete, aiModel: a folder in the model directory, "" for the default).
void HPAIUpdate(void);
// Restarts the helper on the models there now (after a download).
extern "C" void HPAIModelsChanged(void);
// Loaded and answering.
extern "C" bool HPAIReady(void);
// The helper's process, or 0 when it isn't running.
extern "C" int HPAIHelperProcess(void);

// ln P(option | prompt) for each option.
void HPAIRank(NSString* prompt, NSArray<NSString*>* options,
	HPRankHandler handler);
// What likely comes next, "" when the model isn't sure enough.
void HPAIComplete(NSString* prompt, double threshold, HPCompleteHandler handler);
