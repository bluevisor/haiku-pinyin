// 海酷拼音 Haiku Pinyin for macOS: the input controller.
//
// Keys go to the engine first; what it commits is inserted, its composition
// becomes the client's marked text, and its page of candidates goes to the
// system candidate window (IMKCandidates). A Shift tap switches 中/英. The
// settings window (Settings.swift) writes the user defaults; they're applied
// as they change.

#import "InputController.h"

#import <Carbon/Carbon.h>

#include <string>
#include <vector>

#include "../engine/Engine.h"

using haikupinyin::Key;

static const NSTimeInterval kShiftTapLimit = 0.4;
static const double kCloudDelay = 0.12;		// seconds, let typing settle

// Key codes of 1–9, for the candidate window's labels.
static const unsigned short kDigitKeys[] = {
	kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4, kVK_ANSI_5, kVK_ANSI_6,
	kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9,
};


// #pragma mark - shared state


static haikupinyin::Engine* sEngine;
static bool sLoaded;
static IMKCandidates* sCandidates;
static __weak HPInputController* sLast;	// the client the engine state is for
static std::string sCloudAsked;			// in flight
static std::string sCloudAnswered;		// last asked


static NSString*
NS(const std::string& text)
{
	NSString* string = [[NSString alloc] initWithBytes:text.data()
		length:text.size() encoding:NSUTF8StringEncoding];
	return string != nil ? string : @"";
}


static NSString*
SupportDirectory()
{
	NSURL* base = [NSFileManager.defaultManager
		URLForDirectory:NSApplicationSupportDirectory inDomain:NSUserDomainMask
		appropriateForURL:nil create:YES error:nil];
	NSString* path = [base.path stringByAppendingPathComponent:@"HaikuPinyin"];
	[NSFileManager.defaultManager createDirectoryAtPath:path
		withIntermediateDirectories:YES attributes:nil error:nil];
	return path;
}


static void
RegisterDefaults()
{
	haikupinyin::Options options;
	[NSUserDefaults.standardUserDefaults registerDefaults:@{
		@"layout": NS(options.layout),
		@"script": @0,
		@"pageSize": @(options.pageSize),
		@"fullwidth": @(options.fullwidthPunctuation),
		@"emoji": @(options.emoji),
		@"prediction": @(options.prediction),
		@"english": @(options.english),
		@"cloud": @(options.cloud),
		@"fuzzy": @(options.fuzzy),
	}];
}


static haikupinyin::Options
LoadSettings()
{
	NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
	haikupinyin::Options options;
	options.layout = [defaults stringForKey:@"layout"].UTF8String;
	options.script = [defaults integerForKey:@"script"] == 0
		? haikupinyin::Script::kSimplified : haikupinyin::Script::kTraditional;
	NSInteger size = [defaults integerForKey:@"pageSize"];
	if (size >= 3 && size <= 9)
		options.pageSize = int(size);
	options.fullwidthPunctuation = [defaults boolForKey:@"fullwidth"];
	options.emoji = [defaults boolForKey:@"emoji"];
	options.prediction = [defaults boolForKey:@"prediction"];
	options.english = [defaults boolForKey:@"english"];
	options.cloud = [defaults boolForKey:@"cloud"];
	options.fuzzy = unsigned([defaults integerForKey:@"fuzzy"]);
	return options;
}


static bool
SameOptions(const haikupinyin::Options& a, const haikupinyin::Options& b)
{
	return a.layout == b.layout && a.script == b.script
		&& a.pageSize == b.pageSize
		&& a.fullwidthPunctuation == b.fullwidthPunctuation
		&& a.emoji == b.emoji && a.prediction == b.prediction
		&& a.english == b.english && a.cloud == b.cloud && a.fuzzy == b.fuzzy;
}


void
HPSetUp(IMKServer* server)
{
	sEngine = new haikupinyin::Engine;
	NSString* data = NSProcessInfo.processInfo.environment[@"HAIKU_PINYIN_DATA"];
	if (data == nil) {
		data = [NSBundle.mainBundle.resourcePath
			stringByAppendingPathComponent:@"data"];
	}
	std::string error;
	sLoaded = sEngine->Load(data.UTF8String, &error);
	if (!sLoaded) {
		NSLog(@"Haiku Pinyin: %s", error.c_str());
		return;
	}
	sEngine->SetUserDictionary([SupportDirectory()
		stringByAppendingPathComponent:@"user.dict"].UTF8String);
	// Only now can the layout the settings name be checked. (Before the
	// observer below: registering defaults posts a change.)
	RegisterDefaults();
	sEngine->SetOptions(LoadSettings());

	sCandidates = [[IMKCandidates alloc] initWithServer:server
		panelType:kIMKSingleRowSteppingCandidatePanel];
	// Keys come to the controller first; the window only shows the page.
	[sCandidates setAttributes:@{
		IMKCandidatesSendServerKeyEventFirst: @YES,
	}];
	[sCandidates setDismissesAutomatically:NO];
	// The labels (1–9) are the selection keys as this layout types them;
	// without one the window draws none.
	TISInputSourceRef layout = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
	if (layout != NULL) {
		[sCandidates setSelectionKeysKeylayout:layout];
		CFRelease(layout);
	}

	// The settings window changed something.
	[NSNotificationCenter.defaultCenter
		addObserverForName:NSUserDefaultsDidChangeNotification object:nil
		queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
			haikupinyin::Options options = LoadSettings();
			if (SameOptions(options, sEngine->GetOptions()))
				return;
			HPInputController* last = sLast;
			if (last != nil)
				[last commitComposition:nil];
			sEngine->Reset();
			sEngine->SetOptions(options);
			[sCandidates hide];
		}];
}


void
HPSave()
{
	if (sLoaded)
		sEngine->SaveUserDictionary();
}


static bool
MapKey(NSEvent* event, Key* key, char32_t* character, uint32_t* modifiers)
{
	NSEventModifierFlags flags = event.modifierFlags;
	*modifiers = 0;
	if (flags & NSEventModifierFlagShift)
		*modifiers |= haikupinyin::kShift;
	if (flags & NSEventModifierFlagControl)
		*modifiers |= haikupinyin::kControl;
	if (flags & NSEventModifierFlagCommand)
		*modifiers |= haikupinyin::kCommand;
	if (flags & NSEventModifierFlagOption)
		*modifiers |= haikupinyin::kOption;

	*character = 0;
	switch (event.keyCode) {
		case kVK_Delete:
			// Control-Delete forgets a learned word: Mac keyboards have no
			// forward delete.
			*key = (flags & NSEventModifierFlagControl) != 0
				? Key::kDelete : Key::kBackspace;
			return true;
		case kVK_ForwardDelete: *key = Key::kDelete; return true;
		case kVK_Return:
		case kVK_ANSI_KeypadEnter: *key = Key::kReturn; return true;
		case kVK_Space: *key = Key::kSpace; return true;
		case kVK_Escape: *key = Key::kEscape; return true;
		case kVK_Tab: *key = Key::kTab; return true;
		case kVK_LeftArrow: *key = Key::kLeft; return true;
		case kVK_RightArrow: *key = Key::kRight; return true;
		case kVK_UpArrow: *key = Key::kUp; return true;
		case kVK_DownArrow: *key = Key::kDown; return true;
		case kVK_PageUp: *key = Key::kPageUp; return true;
		case kVK_PageDown: *key = Key::kPageDown; return true;
		case kVK_Home: *key = Key::kHome; return true;
		case kVK_End: *key = Key::kEnd; return true;
	}
	// Shortcuts by the key's own character, not what Control makes of it.
	NSString* characters = (*modifiers
			& (haikupinyin::kControl | haikupinyin::kCommand)) != 0
		? event.charactersIgnoringModifiers : event.characters;
	unichar c = characters.length == 1 ? [characters characterAtIndex:0] : 0;
	if (c >= 0x20 && c < 0x7f) {
		*key = Key::kCharacter;
		*character = c;
	} else if (c >= 0x80 && !(c >= 0xf700 && c <= 0xf8ff)) {
		// A non-ASCII character (Option makes many): ends the word, then
		// types itself.
		*key = Key::kCharacter;
		*character = 0x100;
	} else
		*key = Key::kOther;
	return true;
}


// #pragma mark - cloud candidates


// The answer from Google Input Tools: ["SUCCESS",[["<query>",["<answer>",…
static NSString*
CloudAnswer(NSData* data, NSString* query)
{
	if (data == nil)
		return nil;
	id reply = [NSJSONSerialization JSONObjectWithData:data options:0
		error:nil];
	auto array = [](id object, NSUInteger count) {
		return [object isKindOfClass:NSArray.class]
			&& [object count] >= count;
	};
	if (!array(reply, 2) || ![reply[0] isEqual:@"SUCCESS"]
		|| !array(reply[1], 1) || !array(reply[1][0], 2)
		|| ![reply[1][0][0] isEqual:query] || !array(reply[1][0][1], 1)
		|| ![reply[1][0][1][0] isKindOfClass:NSString.class]) {
		return nil;
	}
	return reply[1][0][1][0];
}


// #pragma mark - the controller


@implementation HPInputController {
	BOOL _shiftAlone;
	NSTimeInterval _shiftDown;
	BOOL _markedShown;
	NSArray<NSString*>* _page;
}


- (NSUInteger)recognizedEvents:(id)sender
{
	return NSEventMaskKeyDown | NSEventMaskFlagsChanged;
}


- (id<IMKTextInput>)textClient
{
	return (id<IMKTextInput>)self.client;
}


// The shared engine now serves this client.
- (void)claim
{
	HPInputController* last = sLast;
	if (last == self)
		return;
	if (last != nil)
		[last hideComposition];
	if (sLoaded) {
		sEngine->Reset();
		sEngine->ClearContext();
	}
	[sCandidates hide];
	sLast = self;
}


- (void)hideComposition
{
	if (!_markedShown)
		return;
	[self.textClient setMarkedText:@"" selectionRange:NSMakeRange(0, 0)
		replacementRange:NSMakeRange(NSNotFound, NSNotFound)];
	_markedShown = NO;
}


// Turns the engine's state into client calls: what to insert, the marked
// text, the candidates.
- (void)sync
{
	id<IMKTextInput> client = self.textClient;
	std::string commit = sEngine->TakeCommit();
	if (!commit.empty()) {
		// Replaces the marked text, if any.
		[client insertText:NS(commit)
			replacementRange:NSMakeRange(NSNotFound, NSNotFound)];
		_markedShown = NO;
	}

	if (sEngine->Composing()) {
		std::string preedit = sEngine->Preedit();
		NSString* text = NS(preedit);
		NSUInteger picked = NS(preedit.substr(0,
			sEngine->PickedLength())).length;
		NSMutableAttributedString* marked = [NSMutableAttributedString new];
		// What's picked, then the pinyin still being typed.
		NSRange pickedRange = NSMakeRange(0, picked);
		NSRange rawRange = NSMakeRange(picked, text.length - picked);
		if (picked > 0) {
			[marked appendAttributedString:[[NSAttributedString alloc]
				initWithString:[text substringWithRange:pickedRange]
				attributes:[self markForStyle:kTSMHiliteConvertedText
					atRange:pickedRange]]];
		}
		[marked appendAttributedString:[[NSAttributedString alloc]
			initWithString:[text substringWithRange:rawRange]
			attributes:[self markForStyle:kTSMHiliteSelectedRawText
				atRange:rawRange]]];
		[client setMarkedText:marked
			selectionRange:NSMakeRange(text.length, 0)
			replacementRange:NSMakeRange(NSNotFound, NSNotFound)];
		_markedShown = YES;
	} else
		[self hideComposition];

	[self showCandidates];
}


- (void)showCandidates
{
	if (!sEngine->Composing() && !sEngine->Predicting()) {
		_page = nil;
		[sCandidates hide];
		return;
	}
	[self askCloud];

	std::vector<std::string> page = sEngine->PageCandidates();
	int cloud = sEngine->CloudIndexOnPage();
	NSMutableArray<NSString*>* strings = [NSMutableArray new];
	for (size_t i = 0; i < page.size(); i++) {
		NSString* text = NS(page[i]);
		[strings addObject:int(i) == cloud
			? [text stringByAppendingString:@" ☁"] : text];
	}
	_page = strings;

	NSMutableArray<NSNumber*>* keys = [NSMutableArray new];
	int pageSize = sEngine->GetOptions().pageSize;
	for (int i = 0; i < pageSize && i < 9; i++)
		[keys addObject:@(kDigitKeys[i])];
	[sCandidates setSelectionKeys:keys];
	[sCandidates updateCandidates];
	[sCandidates show:kIMKLocateCandidatesBelowHint];
	int highlight = sEngine->Highlighted();
	[sCandidates selectCandidateWithIdentifier:[sCandidates
		candidateIdentifierAtLineNumber:highlight >= 0 ? highlight : 0]];
}


// IMKCandidates asks for the page it shows.
- (NSArray*)candidates:(id)sender
{
	return _page != nil ? _page : @[];
}


// A click in the candidate window.
- (void)candidateSelected:(NSAttributedString*)candidate
{
	NSUInteger index = [_page indexOfObject:candidate.string];
	if (!sLoaded || sLast != self || index == NSNotFound)
		return;
	sEngine->SelectOnPage(int(index));
	[self sync];
}


- (void)candidateSelectionChanged:(NSAttributedString*)candidate
{
}


// #pragma mark - keys


- (BOOL)handleEvent:(NSEvent*)event client:(id)sender
{
	if (!sLoaded)
		return NO;
	[self claim];

	if (event.type == NSEventTypeFlagsChanged) {
		[self flagsChanged:event];
		return NO;
	}
	if (event.type != NSEventTypeKeyDown)
		return NO;
	_shiftAlone = NO;

	// Caps Lock types English, as the Mac's own methods do.
	if ((event.modifierFlags & NSEventModifierFlagCapsLock) != 0
		&& !sEngine->Composing()) {
		return NO;
	}

	Key key;
	char32_t character;
	uint32_t modifiers;
	MapKey(event, &key, &character, &modifiers);
	if (key == Key::kTab && (modifiers & haikupinyin::kShift) != 0)
		key = Key::kPageUp;

	bool used = sEngine->ProcessKey(key, character, modifiers);
	[self sync];
	return used ? YES : NO;
}


// Shift pressed and let go on its own switches 中/英.
- (void)flagsChanged:(NSEvent*)event
{
	NSEventModifierFlags others = NSEventModifierFlagControl
		| NSEventModifierFlagOption | NSEventModifierFlagCommand;
	NSEventModifierFlags flags = event.modifierFlags;
	bool shiftKey = event.keyCode == kVK_Shift
		|| event.keyCode == kVK_RightShift;
	if (!shiftKey) {
		_shiftAlone = NO;
		return;
	}
	if ((flags & NSEventModifierFlagShift) != 0) {
		_shiftAlone = (flags & others) == 0;
		_shiftDown = event.timestamp;
		return;
	}
	bool tap = _shiftAlone && event.timestamp - _shiftDown <= kShiftTapLimit;
	_shiftAlone = NO;
	if (tap)
		[self toggleChinese];
}


- (void)toggleChinese
{
	bool chinese = !sEngine->Chinese();
	sEngine->SetChinese(chinese);
	[self sync];
	// Which one is on, under the text for a moment.
	NSRect line = NSZeroRect;
	[self.textClient attributesForCharacterIndex:0 lineHeightRectangle:&line];
	HPShowModeBubble(chinese, line.origin.x, line.origin.y);
}


// #pragma mark - focus


- (void)activateServer:(id)sender
{
	[self claim];
}


- (void)deactivateServer:(id)sender
{
	[self commitComposition:sender];
	if (sLast == self) {
		_page = nil;
		[sCandidates hide];
	}
	HPSave();
}


// The client wants the composition ended (a click elsewhere, a switch of
// input source): the letters go in as typed, as with Return.
- (void)commitComposition:(id)sender
{
	if (!sLoaded || sLast != self)
		return;
	if (sEngine->Composing())
		sEngine->ProcessKey(Key::kReturn, 0, 0);
	sEngine->ClearContext();
	[self sync];
}


// #pragma mark - the input menu


- (NSMenu*)menu
{
	NSMenu* menu = [NSMenu new];
	NSMenuItem* mode = [[NSMenuItem alloc]
		initWithTitle:sLoaded && !sEngine->Chinese()
			? @"切换到中文 (Shift)" : @"切换到英文 (Shift)"
		action:@selector(toggleFromMenu:) keyEquivalent:@""];
	[menu addItem:mode];
	[menu addItem:[[NSMenuItem alloc] initWithTitle:@"设置…"
		action:@selector(showPreferences:) keyEquivalent:@""]];
	return menu;
}


- (void)toggleFromMenu:(id)sender
{
	if (!sLoaded)
		return;
	[self claim];
	[self toggleChinese];
}


- (void)showPreferences:(id)sender
{
	HPShowSettings();
}


// #pragma mark - cloud candidates


// One question at a time, asked after a pause so typing doesn't wait for the
// network; the answer joins the candidates if it's still for them.
- (void)askCloud
{
	if (!sEngine->GetOptions().cloud || !sCloudAsked.empty())
		return;
	std::string query = sEngine->CloudQuery();
	if (query.empty() || query == sCloudAnswered)
		return;
	for (char c : query) {
		if (!isalpha(static_cast<unsigned char>(c)))
			return;
	}
	sCloudAsked = query;
	sCloudAnswered = query;

	NSString* text = NS(query);
	NSURLComponents* url = [NSURLComponents
		componentsWithString:@"https://inputtools.google.com/request"];
	url.queryItems = @[
		[NSURLQueryItem queryItemWithName:@"text" value:text],
		[NSURLQueryItem queryItemWithName:@"itc" value:@"zh-t-i0-pinyin"],
		[NSURLQueryItem queryItemWithName:@"num" value:@"1"],
		[NSURLQueryItem queryItemWithName:@"cp" value:@"0"],
		[NSURLQueryItem queryItemWithName:@"cs" value:@"1"],
		[NSURLQueryItem queryItemWithName:@"ie" value:@"utf-8"],
		[NSURLQueryItem queryItemWithName:@"oe" value:@"utf-8"],
		[NSURLQueryItem queryItemWithName:@"app" value:@"haikupinyin"],
	];
	NSMutableURLRequest* request = [NSMutableURLRequest
		requestWithURL:url.URL];
	request.timeoutInterval = 2;

	dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
			int64_t(kCloudDelay * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
		[[NSURLSession.sharedSession dataTaskWithRequest:request
			completionHandler:^(NSData* data, NSURLResponse* response,
				NSError* error) {
			NSString* answer = CloudAnswer(data, text);
			dispatch_async(dispatch_get_main_queue(), ^{
				sCloudAsked.clear();
				HPInputController* last = sLast;
				if (answer != nil && sLoaded
					&& sEngine->SetCloudResult(query, answer.UTF8String)
					&& last != nil) {
					[last showCandidates];
				}
			});
		}] resume];
	});
}


@end
