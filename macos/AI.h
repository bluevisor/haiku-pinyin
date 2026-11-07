// 海酷拼音 Haiku Pinyin for macOS: the AI helper (macos/ai), a separate
// process that holds a small language model. It's started when the settings
// want it and its models are installed, and talked to in JSON lines. Answers
// come back on the main queue; a request a newer one of its kind overtook
// gets none.
#pragma once

#import <Foundation/Foundation.h>

typedef void (^HPRankHandler)(NSArray<NSNumber*>* scores);
typedef void (^HPCompleteHandler)(NSString* text);

// Where the models go: ~/Library/Application Support/HaikuPinyin/models.
NSString* HPAIModelDirectory(void);
// Whether the helper and a model are there to start.
bool HPAIAvailable(void);
// Starts, stops or restarts the helper to match the settings (aiPicks,
// aiComplete, aiModel: a folder in the model directory, "" for the default).
void HPAIUpdate(void);
// Loaded and answering.
bool HPAIReady(void);

// ln P(option | prompt) for each option.
void HPAIRank(NSString* prompt, NSArray<NSString*>* options,
	HPRankHandler handler);
// What likely comes next, "" when the model isn't sure enough.
void HPAIComplete(NSString* prompt, double threshold, HPCompleteHandler handler);
