// 海酷拼音 Haiku Pinyin for macOS.
//   HaikuPinyin              the input method server, started by the system;
//                            opened from anywhere else (the download), it
//                            offers to install itself
//   HaikuPinyin --install    registers the method and enables it; exits 2
//                            when macOS left it for the person to add
//   HaikuPinyin --guide      walks the person through adding it in System
//                            Settings, then switches to it (the .pkg)
//   HaikuPinyin --select     switches to it
//   HaikuPinyin --remove     switches away from it and disables it
//   HaikuPinyin --update     updates this copy now, if there's a newer release
// For testing the install from the download: HP_INPUT_METHODS=<folder>
// installs there instead, without registering it with the system, and
// HP_ASSUME_YES=1 answers its questions with the first button. For testing
// --guide's wait: HP_GUIDE_SOURCE=<input source ID> waits for that one
// instead (com.apple.keylayout.Dvorak can be turned on from code), with
// HP_ASSUME_YES not opening System Settings and not switching.

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


// The one input mode: Settings and the input menu list the mode, not the
// method. (中/英 is a toggle inside it, not a second mode.)
static NSString* const kMode = @"com.bluevisor.inputmethod.HaikuPinyin.Chinese";

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


static bool ModeEnabled();
static bool SourceEnabled(NSString* sourceID);
static int Guide();


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
		// The mode (which enables the method with it). macOS 26 answers
		// noErr and leaves a third-party method off: only the person can add
		// it, in System Settings (--guide).
		for (id source in sources) {
			if ([SourceID(source) isEqualToString:kMode])
				status = TISEnableInputSource((__bridge TISInputSourceRef)source);
		}
		if (status != noErr)
			return 1;
		return ModeEnabled() ? 0 : 2;
	}
	if (strcmp(command, "--guide") == 0)
		return Guide();
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
		// Never left typing in it as it's deleted: to the English layout
		// under it first.
		id current = CFBridgingRelease(TISCopyCurrentKeyboardInputSource());
		NSString* bundle = (__bridge NSString*)TISGetInputSourceProperty(
			(__bridge TISInputSourceRef)current, kTISPropertyBundleID);
		if ([bundle isEqualToString:NSBundle.mainBundle.bundleIdentifier]) {
			id english = CFBridgingRelease(
				TISCopyCurrentASCIICapableKeyboardLayoutInputSource());
			if (english != nil)
				TISSelectInputSource((__bridge TISInputSourceRef)english);
		}
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


// On the enabled list (a fresh one: System Settings changes it).
static bool
SourceEnabled(NSString* sourceID)
{
	NSDictionary* filter = @{
		(__bridge NSString*)kTISPropertyInputSourceID: sourceID,
	};
	NSArray* enabled = CFBridgingRelease(TISCreateInputSourceList(
		(__bridge CFDictionaryRef)filter, false));
	return enabled.count > 0;
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
// macOS wants that done by hand. It asks in the system's language, as the
// input menu is (HPLocalized).
static int
Install()
{
	[NSApplication sharedApplication];
	NSApp.activationPolicy = NSApplicationActivationPolicyRegular;
	NSBundle* bundle = NSBundle.mainBundle;
	NSString* version = [bundle
		objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
	NSString* cancel = HPLocalized(@"Cancel", @"取消", @"取消");
	if (Ask(HPLocalized(@"Install Haiku Pinyin", @"安装海酷拼音", @"安裝海酷拼音"),
			[NSString stringWithFormat:HPLocalized(
				@"Puts Haiku Pinyin %@ in the Input Methods folder "
					"(~/Library/Input Methods) in place of an older version, "
					"and adds it to your input sources.",
				@"把海酷拼音 %@ 装进“输入法”文件夹"
					"（~/Library/Input Methods），换下旧版，并加进输入法列表。",
				@"把海酷拼音 %@ 裝進「輸入法」檔案夾"
					"（~/Library/Input Methods），取代舊版，並加入輸入方式列表。"),
				version],
			@[HPLocalized(@"Install", @"安装", @"安裝"), cancel])
		!= NSAlertFirstButtonReturn) {
		return 0;
	}
	NSString* ok = HPLocalized(@"OK", @"好", @"好");

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
		Ask(HPLocalized(@"Couldn't install", @"无法安装", @"無法安裝"),
			error.localizedDescription, @[ok]);
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

	return Guide();
}


// The steps in System Settings, as the sheet names them.
static NSString*
Steps()
{
	return HPLocalized(
		@"In System Settings › Keyboard, click Edit… next to Input Sources, "
			"then +, choose Chinese, Simplified › Haiku Pinyin, and click Add.",
		@"在“系统设置 › 键盘”里点输入法旁边的“编辑…”，再点 +，"
			"选“简体中文 › 海酷拼音”，然后点“添加”。",
		@"在「系統設定 › 鍵盤」中按一下輸入方式旁邊的「編輯…」，再按 +，"
			"選擇「簡體中文 › 海酷拼音」，然後按「加入」。");
}


static void
OpenKeyboardSettings()
{
	[NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:
		@"x-apple.systempreferences:com.apple.Keyboard-Settings.extension"]];
}


// Switched to it and ready: how to use it.
static void
SayInstalled()
{
	Ask(HPLocalized(@"Haiku Pinyin is installed", @"海酷拼音已安装",
			@"海酷拼音已安裝"),
		HPLocalized(
			@"It's on now. Switch to it with Control-Space or 🌐, and tap "
				"Shift for 中 / 英. Settings, and the AI models' download, are "
				"under Settings… in the input menu in the menu bar.",
			@"现在就能用了。按 Control-空格 或 🌐 键切换到海酷拼音，轻按 Shift "
				"切换中 / 英。设置和 AI 模型下载在菜单栏输入法菜单的“设置…”里。",
			@"現在就能用了。按 Control-空白鍵 或 🌐 鍵切換到海酷拼音，輕按 Shift "
				"切換中 / 英。設定與 AI 模型下載在選單列輸入法選單的「設定…」裡。"),
		@[HPLocalized(@"OK", @"好", @"好")]);
}


static void
SelectMode()
{
	for (id source in Sources()) {
		if ([SourceID(source) isEqualToString:kMode])
			TISSelectInputSource((__bridge TISInputSourceRef)source);
	}
}


// macOS (26 at least) won't let a program add an input method to the input
// sources: only the person can, in System Settings. So this opens it, says
// what to click, and waits, closing by itself once the method is on: then it
// switches to it. Already on (an update): just the switch and the how-to.
static int
Guide()
{
	[NSApplication sharedApplication];
	NSApp.activationPolicy = NSApplicationActivationPolicyRegular;
	const char* testSource = getenv("HP_GUIDE_SOURCE");
	bool testing = getenv("HP_ASSUME_YES") != nullptr;
	NSString* mode = testSource != nullptr ? @(testSource) : kMode;
	if (SourceEnabled(mode)) {
		if (!testing)
			SelectMode();
		SayInstalled();
		return 0;
	}
	NSString* later = HPLocalized(@"Later", @"稍后", @"稍後");
	if (Ask(HPLocalized(@"One more step: add it to your input sources",
				@"还差一步：把它加进输入法列表", @"還差一步：把它加入輸入方式列表"),
			[Steps() stringByAppendingString:HPLocalized(
				@"\n\nmacOS lets only you add an input method.",
				@"\n\nmacOS 只允许你自己添加输入法。",
				@"\n\nmacOS 只允許你自己加入輸入方式。")],
			@[HPLocalized(@"Open System Settings", @"打开系统设置", @"打開系統設定"),
				later])
		!= NSAlertFirstButtonReturn) {
		return 0;
	}
	if (!testing)
		OpenKeyboardSettings();

	// Waits, a modal alert that closes itself once the method is on. The
	// enabled list changes in another process (System Settings); a fresh
	// TISCreateInputSourceList sees it within the second, and the
	// enabled-sources notification says so at once.
	NSAlert* alert = [NSAlert new];
	alert.messageText = HPLocalized(@"Waiting for Haiku Pinyin to be added…",
		@"等你添加海酷拼音……", @"等你加入海酷拼音……");
	alert.informativeText = [Steps() stringByAppendingString:HPLocalized(
		@"\n\nThis closes by itself once it's there.",
		@"\n\n加好后这里会自己关掉。", @"\n\n加好後這裡會自己關閉。")];
	[alert addButtonWithTitle:HPLocalized(@"Cancel", @"取消", @"取消")];
	[alert addButtonWithTitle:HPLocalized(@"Open System Settings Again",
		@"再次打开系统设置", @"再次打開系統設定")];
	static const NSModalResponse kAdded = 1000;
	static const NSTimeInterval kGiveUp = 15 * 60;
	NSDate* start = NSDate.date;
	void (^check)(void) = ^{
		if (SourceEnabled(mode))
			[NSApp stopModalWithCode:kAdded];
		else if (-start.timeIntervalSinceNow > kGiveUp)
			[NSApp stopModalWithCode:NSAlertFirstButtonReturn];
	};
	NSTimer* timer = [NSTimer timerWithTimeInterval:1 repeats:YES
		block:^(NSTimer*) { check(); }];
	[NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
	id observer = [NSDistributedNotificationCenter.defaultCenter
		addObserverForName:(__bridge NSString*)
			kTISNotifyEnabledKeyboardInputSourcesChanged
		object:nil queue:NSOperationQueue.mainQueue
		usingBlock:^(NSNotification*) { check(); }];
	NSModalResponse response;
	// Not brought to the front: System Settings is where the clicks go.
	for (;;) {
		response = [alert runModal];
		if (response != NSAlertSecondButtonReturn)
			break;
		OpenKeyboardSettings();
	}
	[timer invalidate];
	[NSDistributedNotificationCenter.defaultCenter removeObserver:observer];
	if (testing)
		printf("waited %.1f s: %s\n", -start.timeIntervalSinceNow,
			response == kAdded ? "added" : "gave up");
	if (response != kAdded)
		return 0;
	if (!testing)
		SelectMode();
	SayInstalled();
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
