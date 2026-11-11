// 海酷拼音 Haiku Pinyin for macOS: the AI helper's client.

#import "AI.h"

#include <signal.h>

// The model, then a smaller one the helper falls back on when the first is
// too slow on this Mac.
static NSString* const kModel = @"Qwen3-1.7B-Base-4bit";
static NSString* const kBackup = @"Qwen3.5-0.8B-Base-4bit";
static const NSTimeInterval kRestartDelay = 30;	// after it quits

static NSTask* sTask;
static NSString* sChoice;		// the aiModel setting it was started for
static NSFileHandle* sInput;
static dispatch_queue_t sWrites;	// so a stuck helper can't block typing
static bool sReady;
static NSDate* sStarted;
// The model failed to load: not tried again until the models or the choice
// change, as loading a broken one over and over only wastes memory and time.
static bool sFailed;
static int sNextID = 1;
static NSMutableDictionary<NSNumber*, id>* sHandlers;	// id -> block
static NSMutableDictionary<NSNumber*, NSString*>* sOps;	// id -> op
static NSMutableData* sOutput;


NSString*
HPAIModelDirectory()
{
	NSString* path = NSProcessInfo.processInfo.environment[@"HAIKU_PINYIN_MODELS"];
	if (path != nil)
		return path;
	NSURL* base = [NSFileManager.defaultManager
		URLForDirectory:NSApplicationSupportDirectory inDomain:NSUserDomainMask
		appropriateForURL:nil create:YES error:nil];
	return [base.path stringByAppendingPathComponent:@"HaikuPinyin/models"];
}


static NSString*
HelperPath()
{
	NSString* path = NSProcessInfo.processInfo.environment[@"HAIKU_PINYIN_AI"];
	return path != nil ? path
		: [NSBundle.mainBundle pathForAuxiliaryExecutable:@"HaikuPinyinAI"];
}


static NSString*
ModelPath(NSString* name)
{
	NSString* path = [HPAIModelDirectory() stringByAppendingPathComponent:name];
	return [NSFileManager.defaultManager fileExistsAtPath:
		[path stringByAppendingPathComponent:@"config.json"]] ? path : nil;
}


// The aiModel setting: a model folder's name, or "" for the default.
static NSString*
Choice()
{
	return [NSUserDefaults.standardUserDefaults stringForKey:@"aiModel"] ?: @"";
}


bool
HPAIAvailable()
{
	NSString* helper = HelperPath();
	return helper != nil
		&& [NSFileManager.defaultManager isExecutableFileAtPath:helper]
		&& (ModelPath(kModel) != nil || ModelPath(kBackup) != nil
			|| (Choice().length > 0 && ModelPath(Choice()) != nil));
}


NSString* const HPAIStateChanged = @"HPAIStateChanged";


extern "C" bool
HPAIUsable()
{
	return HPAIAvailable() && !sFailed;
}


extern "C" bool
HPAIFailed()
{
	return sFailed;
}


static void
SetFailed(bool failed)
{
	if (sFailed == failed)
		return;
	sFailed = failed;
	[NSNotificationCenter.defaultCenter postNotificationName:HPAIStateChanged
		object:nil];
}


bool
HPAIReady()
{
	return sReady;
}


static void
Forget()
{
	sTask = nil;
	sInput = nil;
	sReady = false;
	[sHandlers removeAllObjects];
	[sOps removeAllObjects];
	sOutput.length = 0;
}


// A line from the helper: an event, or the answer to a request.
static void
Received(NSDictionary* line)
{
	if ([line[@"event"] isEqual:@"ready"]) {
		sReady = true;
		return;
	}
	if ([line[@"event"] isEqual:@"failed"]) {
		sReady = false;
		SetFailed(true);
		return;
	}
	NSNumber* number = line[@"id"];
	if (![number isKindOfClass:NSNumber.class])
		return;
	NSString* op = sOps[number];
	id handler = sHandlers[number];
	// Requests of its kind sent before it were dropped unanswered.
	for (NSNumber* other in sOps.allKeys) {
		if (other.intValue <= number.intValue && [sOps[other] isEqual:op]) {
			[sOps removeObjectForKey:other];
			[sHandlers removeObjectForKey:other];
		}
	}
	if (handler == nil)
		return;
	if ([op isEqual:@"rank"]) {
		NSArray* scores = line[@"scores"];
		if ([scores isKindOfClass:NSArray.class])
			((HPRankHandler)handler)(scores);
	} else if ([op isEqual:@"complete"]) {
		NSString* text = line[@"text"];
		if ([text isKindOfClass:NSString.class])
			((HPCompleteHandler)handler)(text);
	}
}


// force: the setting changed, so no waiting out kRestartDelay.
static void
Start(bool force)
{
	if (sFailed
		|| (!force && sStarted != nil && -sStarted.timeIntervalSinceNow < kRestartDelay))
		return;
	NSString* choice = Choice();
	NSString* model;
	NSString* backup = nil;
	if (choice.length > 0) {
		// The model chosen, alone: it's what was asked for, slow or not.
		model = ModelPath(choice);
	} else {
		model = ModelPath(kModel);
		backup = ModelPath(kBackup);
		if (model == nil) {
			model = backup;
			backup = nil;
		}
	}
	sChoice = choice;
	NSString* helper = HelperPath();
	if (model == nil || helper == nil)
		return;
	if (sHandlers == nil) {
		// A write after it quit mustn't kill the input method.
		signal(SIGPIPE, SIG_IGN);
		sHandlers = [NSMutableDictionary new];
		sOps = [NSMutableDictionary new];
		sOutput = [NSMutableData new];
		sWrites = dispatch_queue_create("HaikuPinyin.ai", DISPATCH_QUEUE_SERIAL);
	}

	NSTask* task = [NSTask new];
	task.executableURL = [NSURL fileURLWithPath:helper];
	task.arguments = backup != nil ? @[model, backup] : @[model];
	NSPipe* input = [NSPipe pipe];
	NSPipe* output = [NSPipe pipe];
	task.standardInput = input;
	task.standardOutput = output;
	NSString* log = [NSHomeDirectory()
		stringByAppendingPathComponent:@"Library/Logs/HaikuPinyinAI.log"];
	[NSFileManager.defaultManager createFileAtPath:log contents:nil
		attributes:nil];
	task.standardError = [NSFileHandle fileHandleForWritingAtPath:log];

	output.fileHandleForReading.readabilityHandler = ^(NSFileHandle* handle) {
		NSData* data = handle.availableData;
		dispatch_async(dispatch_get_main_queue(), ^{
			if (sTask != task)
				return;
			[sOutput appendData:data];
			const char* bytes = (const char*)sOutput.bytes;
			NSUInteger start = 0;
			for (NSUInteger i = 0; i < sOutput.length; i++) {
				if (bytes[i] != '\n')
					continue;
				NSData* line = [sOutput subdataWithRange:NSMakeRange(start, i - start)];
				start = i + 1;
				id object = [NSJSONSerialization JSONObjectWithData:line options:0
					error:nil];
				if ([object isKindOfClass:NSDictionary.class])
					Received(object);
			}
			[sOutput replaceBytesInRange:NSMakeRange(0, start) withBytes:nullptr
				length:0];
		});
	};
	task.terminationHandler = ^(NSTask* ended) {
		output.fileHandleForReading.readabilityHandler = nil;
		dispatch_async(dispatch_get_main_queue(), ^{
			if (sTask == ended) {
				NSLog(@"Haiku Pinyin: the AI helper quit (%d)", ended.terminationStatus);
				// Gone before its model loaded: it would only fail again.
				if (!sReady)
					SetFailed(true);
				Forget();
			}
		});
	};

	NSError* error;
	sStarted = NSDate.date;
	if (![task launchAndReturnError:&error]) {
		NSLog(@"Haiku Pinyin: can't start the AI helper: %@", error);
		SetFailed(true);
		return;
	}
	sTask = task;
	sInput = input.fileHandleForWriting;
}


static void
Stop()
{
	if (sTask == nil)
		return;
	NSFileHandle* input = sInput;
	Forget();
	// It quits when its input closes.
	dispatch_async(sWrites, ^{
		[input closeFile];
	});
}


void
HPAIUpdate()
{
	NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
	bool wanted = [defaults boolForKey:@"aiPicks"]
		|| [defaults boolForKey:@"aiComplete"];
	if (!wanted) {
		Stop();
		return;
	}
	// Another model chosen: worth a try even after one failed.
	bool changed = ![sChoice isEqual:Choice()];
	if (changed)
		SetFailed(false);
	if (sTask == nil)
		Start(changed);
	else if (changed) {
		Stop();
		Start(true);
	}
}


// Models were added (the settings window downloaded them): start over with
// them now, not after kRestartDelay.
extern "C" void
HPAIModelsChanged(void)
{
	Stop();
	SetFailed(false);
	NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
	if ([defaults boolForKey:@"aiPicks"] || [defaults boolForKey:@"aiComplete"])
		Start(true);
}


static void
Send(NSMutableDictionary* request, NSString* op, id handler)
{
	if (sTask == nil) {
		HPAIUpdate();	// it quit a while ago: try again
		return;
	}
	if (!sReady)
		return;
	NSNumber* number = @(sNextID++);
	request[@"id"] = number;
	request[@"op"] = op;
	sOps[number] = op;
	sHandlers[number] = [handler copy];
	NSMutableData* line = [[NSJSONSerialization dataWithJSONObject:request
		options:0 error:nil] mutableCopy];
	[line appendBytes:"\n" length:1];
	NSFileHandle* input = sInput;
	dispatch_async(sWrites, ^{
		@try {
			[input writeData:line];
		} @catch (NSException*) {
			// It quit; the termination handler tidies up.
		}
	});
}


void
HPAIRank(NSString* prompt, NSArray<NSString*>* options, HPRankHandler handler)
{
	Send([@{@"prompt": prompt, @"options": options} mutableCopy], @"rank",
		handler);
}


void
HPAIComplete(NSString* prompt, double threshold, HPCompleteHandler handler)
{
	Send([@{@"prompt": prompt, @"threshold": @(threshold)} mutableCopy],
		@"complete", handler);
}
