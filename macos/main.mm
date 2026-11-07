// 海酷拼音 Haiku Pinyin for macOS.
//   HaikuPinyin              the input method server, started by the system
//   HaikuPinyin --install    registers the method and enables it
//   HaikuPinyin --select     switches to it
//   HaikuPinyin --remove     disables it

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <InputMethodKit/InputMethodKit.h>

#include <cstdio>
#include <cstring>

#import "InputController.h"

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
	if (strcmp(command, "--version") == 0) {
		NSString* version = [NSBundle.mainBundle
			objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
		printf("HaikuPinyin %s\n", version.UTF8String);
		return 0;
	}
	fprintf(stderr, "usage: HaikuPinyin [--install | --select | --remove]\n");
	return 1;
}


int
main(int argc, const char* argv[])
{
	@autoreleasepool {
		if (argc > 1)
			return Command(argv[1]);

		NSBundle* bundle = NSBundle.mainBundle;
		IMKServer* server = [[IMKServer alloc]
			initWithName:[bundle objectForInfoDictionaryKey:
				@"InputMethodConnectionName"]
			bundleIdentifier:bundle.bundleIdentifier];
		HPSetUp(server);

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
