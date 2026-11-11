// 海酷拼音 Haiku Pinyin for macOS: custom phrases, importing and exporting
// learned words, and the sync folder, for the settings window (Words.swift,
// which says what happened in the system's language). A second engine works
// on the files; the input method's own takes the change up as its next
// composition starts.

#import <Foundation/Foundation.h>

#include <string>

#include "../engine/Engine.h"


// ~/Library/Application Support/HaikuPinyin, as InputController.mm has it.
static NSString*
Folder()
{
	NSURL* base = [NSFileManager.defaultManager
		URLForDirectory:NSApplicationSupportDirectory inDomain:NSUserDomainMask
		appropriateForURL:nil create:YES error:nil];
	NSString* path = [base.path stringByAppendingPathComponent:@"HaikuPinyin"];
	[NSFileManager.defaultManager createDirectoryAtPath:path
		withIntermediateDirectories:YES attributes:nil error:nil];
	return path;
}


static bool
LoadEngine(haikupinyin::Engine& engine)
{
	NSString* data = NSProcessInfo.processInfo.environment[@"HAIKU_PINYIN_DATA"];
	if (data == nil) {
		data = [NSBundle.mainBundle.resourcePath
			stringByAppendingPathComponent:@"data"];
	}
	if (!engine.Load(data.UTF8String))
		return false;
	engine.SetUserDictionary([Folder()
		stringByAppendingPathComponent:@"user.dict"].UTF8String);
	return true;
}


// The phrases file, made with a commented example when there's none.
extern "C" const char*
HPPhrasesFile(void)
{
	static std::string path;
	path = [Folder() stringByAppendingPathComponent:@"phrases.txt"].UTF8String;
	haikupinyin::Phrases phrases;
	phrases.SetPath(path);
	phrases.CreateFile();
	return path.c_str();
}


// What the file was read as (Engine::ImportResult::format, "" for nothing),
// the counts, and the problem (haikupinyin::ImportProblem; -1: the
// dictionaries didn't load).
extern "C" const char*
HPImportWords(const char* path, int32_t* words, int32_t* phrases,
	int32_t* skipped, int32_t* problem)
{
	static thread_local std::string format;
	haikupinyin::Engine engine;
	*words = *phrases = *skipped = 0;
	if (!LoadEngine(engine)) {
		*problem = -1;
		format.clear();
		return format.c_str();
	}
	haikupinyin::Engine::ImportResult result = engine.ImportWords(path);
	*words = result.words;
	*phrases = result.phrases;
	*skipped = result.skipped;
	*problem = int32_t(result.problem);
	format = result.format;
	return format.c_str();
}


// How many words went out, or -1.
extern "C" int32_t
HPExportWords(const char* path)
{
	haikupinyin::Engine engine;
	int count = 0;
	if (!LoadEngine(engine) || !engine.ExportWords(path, &count))
		return -1;
	return count;
}


// The sync folder (engine/Sync.h): the sync-folder file beside user.dict,
// "" when off. The input method reads it again as a composition starts.
extern "C" const char*
HPSyncFolder(void)
{
	static std::string folder;
	folder = haikupinyin::ReadSyncSetting([Folder()
		stringByAppendingPathComponent:@"user.dict"].UTF8String);
	return folder.c_str();
}


extern "C" bool
HPSetSyncFolder(const char* folder)
{
	return haikupinyin::WriteSyncSetting([Folder()
		stringByAppendingPathComponent:@"user.dict"].UTF8String, folder);
}
