// 海酷拼音 Haiku Pinyin for macOS: custom phrases, and importing and
// exporting learned words, for the settings window (Words.swift). A second
// engine works on the files; the input method's own takes the change up as
// its next composition starts.

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
LoadEngine(haikupinyin::Engine& engine, std::string* error)
{
	NSString* data = NSProcessInfo.processInfo.environment[@"HAIKU_PINYIN_DATA"];
	if (data == nil) {
		data = [NSBundle.mainBundle.resourcePath
			stringByAppendingPathComponent:@"data"];
	}
	if (!engine.Load(data.UTF8String, error))
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


// What happened, in a sentence for the settings window.
extern "C" const char*
HPImportWords(const char* path)
{
	static thread_local std::string summary;
	haikupinyin::Engine engine;
	std::string error;
	if (!LoadEngine(engine, &error))
		summary = "没能导入：" + error;
	else
		summary = engine.ImportWords(path).Summary();
	return summary.c_str();
}


extern "C" const char*
HPExportWords(const char* path)
{
	static thread_local std::string summary;
	haikupinyin::Engine engine;
	std::string error;
	int count = 0;
	if (!LoadEngine(engine, &error))
		summary = "没能导出：" + error;
	else if (!engine.ExportWords(path, &count))
		summary = std::string("没能写入 ") + path;
	else
		summary = "导出了 " + std::to_string(count) + " 个学到的词。";
	return summary.c_str();
}
