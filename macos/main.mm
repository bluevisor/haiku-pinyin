// 海酷拼音 Haiku Pinyin for macOS.
//   HaikuPinyin              the input method server, started by the system;
//                            opened from anywhere else (the download), it
//                            offers to install itself
//   HaikuPinyin --install    registers the method and enables it
//   HaikuPinyin --select     switches to it
//   HaikuPinyin --remove     disables it
//   HaikuPinyin --update     updates this copy now, if there's a newer release
// For testing the install from the download: HP_INPUT_METHODS=<folder>
// installs there instead, without registering it with the system, and
// HP_ASSUME_YES=1 answers its questions with the first button.

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <InputMethodKit/InputMethodKit.h>

#include <cstdio>
#include <cstring>

#import "InputController.h"

// Updater.swift
extern "C" void HPUpdaterStart(void);
extern "C" int HPUpdaterRunOnce(void);

// For Updater.swift, which can't call C++.
extern "C" void
HPSaveNow(void)
{
	HPSave();
}


// Settings and the input menu list the 中 mode, not the method.
static NSString* const kMode = HPChineseMode;

static NSArray*
Sources()
{
	NSDictionary* filter = @{
		(__bridge NSString*)kTISPropertyBundleID:
			NSBundle.mainBundle.bundleIdentifier,
	};
	return CFBridgingRelease(TISCreateInputSourceList(
		(__bridge CFDictionaryRef)filter, true));
}


static NSString*
SourceID(id source)
{
	return (__bridge NSString*)TISGetInputSourceProperty(
		(__bridge TISInputSourceRef)source, kTISPropertyInputSourceID);
}


// The hidden 英 mode has to be on for the toggle to select it. The server
// checks as it starts too: the method may have been added in Settings, or
// updated from a version without the mode (registering again reads it).
static void
EnableEnglishMode()
{
	for (int pass = 0; pass < 2; pass++) {
		for (id source in Sources()) {
			if (![SourceID(source) isEqualToString:HPEnglishMode])
				continue;
			TISInputSourceRef ref = (__bridge TISInputSourceRef)source;
			CFBooleanRef enabled = (CFBooleanRef)TISGetInputSourceProperty(
				ref, kTISPropertyInputSourceIsEnabled);
			if (enabled == nullptr || !CFBooleanGetValue(enabled))
				TISEnableInputSource(ref);
			return;
		}
		TISRegisterInputSource((__bridge CFURLRef)NSBundle.mainBundle.bundleURL);
	}
}


static int
Command(const char* command)
{
	if (strcmp(command, "--install") == 0) {
		OSStatus status = TISRegisterInputSource(
			(__bridge CFURLRef)NSBundle.mainBundle.bundleURL);
		if (status != noErr) {
			fprintf(stderr, "HaikuPinyin: can't register (%d)\n", int(status));
			return 1;
		}
		NSArray* sources = Sources();
		// The mode (which enables the method with it).
		for (id source in sources) {
			if ([SourceID(source) isEqualToString:kMode])
				status = TISEnableInputSource((__bridge TISInputSourceRef)source);
		}
		EnableEnglishMode();
		return status == noErr ? 0 : 1;
	}
	if (strcmp(command, "--select") == 0) {
		for (id source in Sources()) {
			if ([SourceID(source) isEqualToString:kMode]) {
				return TISSelectInputSource((__bridge TISInputSourceRef)source)
					== noErr ? 0 : 1;
			}
		}
		fprintf(stderr, "HaikuPinyin: not installed\n");
		return 1;
	}
	if (strcmp(command, "--remove") == 0) {
		for (id source in Sources())
			TISDisableInputSource((__bridge TISInputSourceRef)source);
		return 0;
	}
	if (strcmp(command, "--update") == 0)
		return HPUpdaterRunOnce();
	if (strcmp(command, "--version") == 0) {
		NSString* version = [NSBundle.mainBundle
			objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
		printf("HaikuPinyin %s\n", version.UTF8String);
		return 0;
	}
	fprintf(stderr, "usage: HaikuPinyin [--install | --select | --remove | --update]\n");
	return 1;
}


// #pragma mark - installing from the download


// Where the system runs input methods from: ~/Library/Input Methods or
// /Library/Input Methods.
static bool
Installed()
{
	return [NSBundle.mainBundle.bundlePath.stringByDeletingLastPathComponent
		.lastPathComponent isEqualToString:@"Input Methods"];
}


static int
Run(NSString* path, NSArray<NSString*>* arguments)
{
	NSTask* task = [NSTask new];
	task.executableURL = [NSURL fileURLWithPath:path];
	task.arguments = arguments;
	if (![task launchAndReturnError:nil])
		return -1;
	[task waitUntilExit];
	return task.terminationStatus;
}


static bool
ModeEnabled()
{
	for (id source in Sources()) {
		if (![SourceID(source) isEqualToString:kMode])
			continue;
		CFBooleanRef enabled = (CFBooleanRef)TISGetInputSourceProperty(
			(__bridge TISInputSourceRef)source, kTISPropertyInputSourceIsEnabled);
		return enabled != nullptr && CFBooleanGetValue(enabled);
	}
	return false;
}


static NSModalResponse
Ask(NSString* title, NSString* text, NSArray<NSString*>* buttons)
{
	if (getenv("HP_ASSUME_YES") != nullptr) {
		printf("%s: %s\n", title.UTF8String, text.UTF8String);
		return NSAlertFirstButtonReturn;
	}
	NSAlert* alert = [NSAlert new];
	alert.messageText = title;
	alert.informativeText = text;
	for (NSString* button in buttons)
		[alert addButtonWithTitle:button];
	[NSApp activateIgnoringOtherApps:YES];
	return [alert runModal];
}


// Opened from the disk image or Downloads: copies itself into
// ~/Library/Input Methods in place of an older copy, registers and turns on
// the method there, and says how to switch to it, or where to add it when
// macOS wants that done by hand.
static int
Install()
{
	[NSApplication sharedApplication];
	NSApp.activationPolicy = NSApplicationActivationPolicyRegular;
	NSBundle* bundle = NSBundle.mainBundle;
	NSString* version = [bundle
		objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
	if (Ask(@"安装海酷拼音 Haiku Pinyin",
			[NSString stringWithFormat:@"把海酷拼音 %@ 装进“输入法”文件夹"
				"（~/Library/Input Methods），换下旧版，并加进输入法列表。", version],
			@[@"安装", @"取消"]) != NSAlertFirstButtonReturn) {
		return 0;
	}

	NSFileManager* manager = NSFileManager.defaultManager;
	const char* testFolder = getenv("HP_INPUT_METHODS");
	NSString* folder = testFolder != nullptr ? @(testFolder)
		: [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Input Methods"];
	NSString* dest = [folder
		stringByAppendingPathComponent:bundle.bundlePath.lastPathComponent];
	// The old copy quits first; the system starts the new one when it's used.
	pid_t me = NSProcessInfo.processInfo.processIdentifier;
	for (NSRunningApplication* app in [NSRunningApplication
			runningApplicationsWithBundleIdentifier:bundle.bundleIdentifier]) {
		if (app.processIdentifier != me && testFolder == nullptr)
			[app forceTerminate];
	}
	NSError* error;
	[manager createDirectoryAtPath:folder withIntermediateDirectories:YES
		attributes:nil error:nil];
	[manager removeItemAtPath:dest error:nil];
	if (![manager copyItemAtPath:bundle.bundlePath toPath:dest error:&error]) {
		Ask(@"无法安装", error.localizedDescription, @[@"好"]);
		return 1;
	}
	// It passed Gatekeeper to get here; the system starts the copy in the
	// background, where a "downloaded from the internet" question can't be
	// answered.
	Run(@"/usr/bin/xattr", @[@"-dr", @"com.apple.quarantine", dest]);
	if (testFolder != nullptr) {
		printf("installed in %s, not registered\n", dest.UTF8String);
		return 0;
	}
	NSString* binary = [[dest stringByAppendingPathComponent:@"Contents/MacOS"]
		stringByAppendingPathComponent:bundle.executablePath.lastPathComponent];
	Run(binary, @[@"--install"]);
	Run(binary, @[@"--select"]);

	if (ModeEnabled()) {
		Ask(@"海酷拼音已安装",
			@"按 Control-空格 或 🌐 键切换到海酷拼音，轻按 Shift 切换中 / 英。"
				"设置和 AI 模型下载在菜单栏输入法菜单的“设置…”里。",
			@[@"好"]);
	} else if (Ask(@"还差一步",
			@"在“系统设置 › 键盘 › 输入法”里点“编辑…”，再点 +，"
				"选“简体中文 › 海酷拼音”。",
			@[@"打开系统设置", @"稍后"]) == NSAlertFirstButtonReturn) {
		[NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:
			@"x-apple.systempreferences:com.apple.Keyboard-Settings.extension"]];
	}
	return 0;
}


int
main(int argc, const char* argv[])
{
	@autoreleasepool {
		if (argc > 1 && strncmp(argv[1], "--", 2) == 0)
			return Command(argv[1]);
		// Opened by hand (Finder may pass -psn_…), not by the system.
		if (!Installed())
			return Install();

		NSBundle* bundle = NSBundle.mainBundle;
		IMKServer* server = [[IMKServer alloc]
			initWithName:[bundle objectForInfoDictionaryKey:
				@"InputMethodConnectionName"]
			bundleIdentifier:bundle.bundleIdentifier];
		HPSetUp(server);
		EnableEnglishMode();
		HPUpdaterStart();

		[NSApplication sharedApplication];
		[NSNotificationCenter.defaultCenter
			addObserverForName:NSApplicationWillTerminateNotification
			object:nil queue:nil usingBlock:^(NSNotification*) {
				HPSave();
			}];
		[NSApp run];
		(void)server;
	}
	return 0;
}
