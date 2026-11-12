// 海酷拼音 Haiku Pinyin for macOS: the input controller.
//
// Keys go to the engine first; what it commits is inserted, its composition
// becomes the client's marked text, and its page of candidates goes to the
// candidate window (Candidates.swift). A Shift tap switches 中/英. The
// settings window (Settings.swift) writes the user defaults; they're applied
// as they change. With the AI helper (AI.mm) running, a language model that
// reads the text before the cursor moves the candidate it likes first, and
// offers what likely comes after a commit: gray text after the cursor, taken
// with Tab, gone with any other key.

#import "InputController.h"
#import "AI.h"

#import <Carbon/Carbon.h>

#include <string>
#include <vector>

#include "../engine/Engine.h"

using haikupinyin::Key;

static const NSTimeInterval kShiftTapLimit = 0.4;
static const double kCloudDelay = 0.12;		// seconds, let typing settle
// How sure the AI must be of a completion, all of it, to offer it. Gray text
// is easy to type past, so a fair guess is worth showing.
static const double kAIThreshold = 0.3;
static const NSUInteger kContextLength = 64;	// characters before the cursor

// #pragma mark - shared state


// `defaults write com.bluevisor.inputmethod.HaikuPinyin debugLog -bool YES`
// and restart it: every key, where it came from and what was sent back, in
// ~/Library/Logs/HaikuPinyin.log.
static bool sDebug;
static FILE* sDebugFile;
#define HPDebug(...) do { if (sDebug) DebugLine([NSString stringWithFormat:__VA_ARGS__]); } while (0)

static void
DebugLine(NSString* line)
{
	if (sDebugFile == nullptr) {
		NSString* path = [NSHomeDirectory()
			stringByAppendingPathComponent:@"Library/Logs/HaikuPinyin.log"];
		sDebugFile = fopen(path.fileSystemRepresentation, "a");
		if (sDebugFile == nullptr)
			return;
	}
	NSDateFormatter* format = [NSDateFormatter new];
	format.dateFormat = @"HH:mm:ss.SSS";
	fprintf(sDebugFile, "%s %s\n", [format stringFromDate:NSDate.date].UTF8String,
		line.UTF8String);
	fflush(sDebugFile);
}


static haikupinyin::Engine* sEngine;
static bool sLoaded;
static __weak HPInputController* sLast;	// the client the engine state is for
static std::string sCloudAsked;			// in flight
static std::string sCloudAnswered;		// last asked
static uint32_t sAIPickAsked = UINT32_MAX;	// the candidates' revision
static std::string sAICompletionAsked;		// the text


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
		@"english": @(options.english),
		@"cloud": @(options.cloud),
		@"fuzzy": @(options.fuzzy),
		@"vowels": @(int(options.vowels)),
		@"aiPicks": @YES,
		@"aiComplete": @YES,
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
	options.english = [defaults boolForKey:@"english"];
	options.cloud = [defaults boolForKey:@"cloud"];
	options.fuzzy = unsigned([defaults integerForKey:@"fuzzy"]);
	NSInteger vowels = [defaults integerForKey:@"vowels"];
	if (vowels >= 0 && vowels <= int(haikupinyin::Vowels::kSingle))
		options.vowels = haikupinyin::Vowels(vowels);
	return options;
}


static bool
SameOptions(const haikupinyin::Options& a, const haikupinyin::Options& b)
{
	return a.layout == b.layout && a.script == b.script
		&& a.pageSize == b.pageSize
		&& a.fullwidthPunctuation == b.fullwidthPunctuation
		&& a.emoji == b.emoji
		&& a.english == b.english && a.cloud == b.cloud && a.fuzzy == b.fuzzy
		&& a.vowels == b.vowels;
}


@interface HPInputController ()
- (void)sync;
- (void)showCandidates;
@end


// A click in the candidate window.
static void
PickCandidate(int32_t index)
{
	HPInputController* last = sLast;
	if (!sLoaded || last == nil)
		return;
	sEngine->SelectOnPage(index);
	[last sync];
}


// Its arrows and the scroll wheel.
static void
TurnPage(int32_t delta)
{
	HPInputController* last = sLast;
	if (!sLoaded || last == nil)
		return;
	sEngine->ProcessKey(delta > 0 ? Key::kPageDown : Key::kPageUp, 0, 0);
	[last showCandidates];
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
	sDebug = [NSUserDefaults.standardUserDefaults boolForKey:@"debugLog"];
	HPAIUpdate();

	HPSetCandidateHandlers(PickCandidate, TurnPage);

	// The settings window changed something.
	[NSNotificationCenter.defaultCenter
		addObserverForName:NSUserDefaultsDidChangeNotification object:nil
		queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
			HPAIUpdate();
			haikupinyin::Options options = LoadSettings();
			if (SameOptions(options, sEngine->GetOptions()))
				return;
			HPInputController* last = sLast;
			if (last != nil)
				[last commitComposition:nil];
			sEngine->Reset();
			sEngine->SetOptions(options);
			HPHideCandidates();
		}];
}


bool
HPComposing()
{
	return sLoaded && sEngine->Composing();
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
	__weak id _client;			// where the last event came from
	BOOL _shiftAlone;
	NSTimeInterval _shiftDown;
	BOOL _markedShown;
}


- (void)takeClient:(id)sender
{
	if ([sender respondsToSelector:@selector(insertText:replacementRange:)])
		_client = sender;
}


- (NSUInteger)recognizedEvents:(id)sender
{
	return NSEventMaskKeyDown | NSEventMaskFlagsChanged
		| NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown;
}


// The text field the last key came from. IMK passes it with each event, and
// it isn't always self.client: answering that instead sends the composition
// to another view of the app (winit apps such as Temino then type the raw
// letters).
- (id<IMKTextInput>)textClient
{
	id client = _client != nil ? _client : self.client;
	return (id<IMKTextInput>)client;
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
	HPHideCandidates();
	HPHideGhost();
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
		HPDebug(@"insertText %s to %p", commit.c_str(), client);
		_markedShown = NO;
	}

	std::string suggestion = sEngine->Suggestion();
	if (suggestion.empty() || sEngine->Composing())
		HPHideGhost();
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
		HPDebug(@"setMarkedText %@ to %p", text, client);
		_markedShown = YES;
	} else {
		[self hideComposition];
		if (!suggestion.empty())
			[self showSuggestion:suggestion];
	}

	[self showCandidates];
}


// The AI's completion, in gray just after the cursor (Candidates.swift). It
// isn't in the text until Tab: any other key or a click takes it away.
- (void)showSuggestion:(const std::string&)suggestion
{
	NSRect line = NSZeroRect;
	NSDictionary* attributes = [self.textClient attributesForCharacterIndex:0
		lineHeightRectangle:&line];
	NSFont* font = attributes[NSFontAttributeName];
	if (![font isKindOfClass:NSFont.class])
		font = nil;
	HPShowGhost(suggestion.c_str(), line.origin.x, line.origin.y,
		line.size.height, font != nil ? font.fontName.UTF8String : "",
		font != nil ? font.pointSize : 0);
	HPDebug(@"suggestion %@ at %@, font %@", NS(suggestion),
		NSStringFromRect(line), font);
}


// Whether there's text after the cursor on its line, which gray text would
// cover. Clients that can't say (many terminals) have the cursor at the end.
- (BOOL)textAfterCursor
{
	id<IMKTextInput> client = self.textClient;
	NSRange selection = [client selectedRange];
	if (selection.location == NSNotFound)
		return NO;
	NSAttributedString* after = [client attributedSubstringFromRange:
		NSMakeRange(selection.location + selection.length, 1)];
	if (after.length == 0)
		return NO;
	unichar c = [after.string characterAtIndex:0];
	return c != '\n' && c != '\r' && c != 0x2028 && c != 0x2029;
}


- (void)showCandidates
{
	// Under the start of the composition, or the cursor.
	NSRect line = NSZeroRect;
	[self.textClient attributesForCharacterIndex:0 lineHeightRectangle:&line];
	if (!sEngine->Composing()) {
		HPHideCandidates();
		if (sEngine->Suggestion().empty())
			[self askAICompletion];
		return;
	}
	[self askCloud];
	[self askAIPick];

	std::string joined;
	for (const std::string& candidate : sEngine->PageCandidates()) {
		if (!joined.empty())
			joined += '\n';
		joined += candidate;
	}
	HPShowCandidates(joined.c_str(), sEngine->Highlighted(),
		sEngine->CloudIndexOnPage(), sEngine->AIIndexOnPage(),
		sEngine->HasPreviousPage(),
		sEngine->HasNextPage(), line.origin.x, line.origin.y,
		line.size.height, sEngine->PickKeys().c_str());
}


// #pragma mark - keys


- (BOOL)handleEvent:(NSEvent*)event client:(id)sender
{
	[self takeClient:sender];
	if (!sLoaded)
		return NO;
	[self claim];
	if (event.type == NSEventTypeKeyDown && (event.modifierFlags
			& NSEventModifierFlagCapsLock) != 0) {
		HPDebug(@"key %hu with Caps Lock: English", event.keyCode);
	}

	if (event.type == NSEventTypeFlagsChanged) {
		[self flagsChanged:event];
		return NO;
	}
	// A click moves the cursor: a completion on offer goes first, so the
	// client never keeps it as text.
	if (event.type == NSEventTypeLeftMouseDown
		|| event.type == NSEventTypeRightMouseDown) {
		if (!sEngine->Composing() && !sEngine->Suggestion().empty()) {
			sEngine->ClearContext();
			[self sync];
		}
		return NO;
	}
	if (event.type != NSEventTypeKeyDown)
		return NO;
	_shiftAlone = NO;

	bool offered = !sEngine->Composing() && !sEngine->Suggestion().empty();
	// Caps Lock types English, as the Mac's own methods do.
	if ((event.modifierFlags & NSEventModifierFlagCapsLock) != 0
		&& !sEngine->Composing()) {
		if (offered) {
			sEngine->ClearContext();
			[self sync];
		}
		return NO;
	}

	Key key;
	char32_t character;
	uint32_t modifiers;
	MapKey(event, &key, &character, &modifiers);
	if (key == Key::kTab && (modifiers & haikupinyin::kShift) != 0)
		key = Key::kPageUp;

	// A word starts: what's before the cursor is its context.
	if (!sEngine->Composing() && key == Key::kCharacter && sEngine->Chinese())
		[self readContext];
	bool used = sEngine->ProcessKey(key, character, modifiers);
	if (sEngine->TakeScriptToggled())
		[self scriptToggled];
	// Escape just takes a completion away.
	if (offered && key == Key::kEscape)
		used = true;
	HPDebug(@"key %hu %@ flags %lx from %@ (%p, self.client %p): %s, composing %d, chinese %d",
		event.keyCode, event.characters, (unsigned long)event.modifierFlags,
		[sender respondsToSelector:@selector(bundleIdentifier)]
			? [sender bundleIdentifier] : @"?",
		sender, self.client, used ? "used" : "passed", sEngine->Composing(),
		sEngine->Chinese());
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
	if (tap) {
		HPDebug(@"Shift tap: %s", sEngine->Chinese() ? "英" : "中");
		[self toggleChinese];
	}
}


- (void)toggleChinese
{
	bool chinese = !sEngine->Chinese();
	sEngine->SetChinese(chinese);
	[self sync];
	[self showMode];
}


// Which one is on, under the text for a moment.
- (void)showMode
{
	NSRect line = NSZeroRect;
	[self.textClient attributesForCharacterIndex:0 lineHeightRectangle:&line];
	HPShowModeBubble(sEngine->Chinese(), line.origin.x, line.origin.y);
}


// Control-Shift-F switched 简/繁 (the engine took it): saved as the
// setting, which the defaults observer then finds already applied, and
// shown like 中/英.
- (void)scriptToggled
{
	bool traditional
		= sEngine->GetOptions().script != haikupinyin::Script::kSimplified;
	HPDebug(@"Control-Shift-F: %s", traditional ? "繁" : "简");
	[NSUserDefaults.standardUserDefaults setInteger:traditional ? 1 : 0
		forKey:@"script"];
	NSRect line = NSZeroRect;
	[self.textClient attributesForCharacterIndex:0 lineHeightRectangle:&line];
	HPShowScriptBubble(traditional, line.origin.x, line.origin.y);
}


// #pragma mark - focus


- (void)activateServer:(id)sender
{
	[self takeClient:sender];
	HPDebug(@"activate %@ (%p)", [sender respondsToSelector:@selector(bundleIdentifier)]
		? [sender bundleIdentifier] : @"?", sender);
	[self claim];
	// English is easy to forget it's on: say so on arriving in a field.
	if (sLoaded && !sEngine->Chinese())
		[self showMode];
}


- (void)deactivateServer:(id)sender
{
	[self commitComposition:sender];
	if (sLast == self) {
		HPHideCandidates();
		HPHideGhost();
	}
	HPSave();
}


// The client wants the composition ended (a click elsewhere, a switch of
// input source): the letters go in as typed, as with Return.
- (void)commitComposition:(id)sender
{
	[self takeClient:sender];
	if (!sLoaded || sLast != self)
		return;
	if (sEngine->Composing())
		sEngine->ProcessKey(Key::kReturn, 0, 0);
	sEngine->ClearContext();
	[self sync];
}


// #pragma mark - the input menu


// The menu in the system's language: 简体 or 繁體 Chinese, else English.
static NSString*
Menu(NSString* english, NSString* simplified, NSString* traditional)
{
	static NSString* language = [NSBundle
		preferredLocalizationsFromArray:@[ @"en", @"zh-Hans", @"zh-Hant" ]
		forPreferences:NSLocale.preferredLanguages].firstObject;
	if ([language isEqualToString:@"zh-Hans"])
		return simplified;
	if ([language isEqualToString:@"zh-Hant"])
		return traditional;
	return english;
}


- (NSMenu*)menu
{
	NSMenu* menu = [NSMenu new];
	[menu addItem:[[NSMenuItem alloc]
		initWithTitle:Menu(@"Settings…", @"设置…", @"設定…")
		action:@selector(showPreferences:) keyEquivalent:@""]];
	[menu addItem:[[NSMenuItem alloc]
		initWithTitle:Menu(@"Check for Updates…", @"检查更新…", @"檢查更新…")
		action:@selector(checkForUpdates:) keyEquivalent:@""]];
	[menu addItem:[[NSMenuItem alloc]
		initWithTitle:sLoaded && !sEngine->Chinese()
			? Menu(@"Switch to Chinese (Shift)",
				@"现在是英文：切换到中文 (Shift)", @"現在是英文：切換到中文 (Shift)")
			: Menu(@"Switch to English (Shift)",
				@"切换到英文 (Shift)", @"切換到英文 (Shift)")
		action:@selector(toggleFromMenu:) keyEquivalent:@""]];
	// 简体 / 繁體, ticked: the same setting as 字形 in the settings window.
	// The other one shows Control-Shift-F, the key that switches to it.
	[menu addItem:NSMenuItem.separatorItem];
	NSInteger script = [NSUserDefaults.standardUserDefaults
		integerForKey:@"script"];
	NSArray* scripts = @[
		Menu(@"Simplified Chinese", @"简体中文", @"簡體中文"),
		Menu(@"Traditional Chinese", @"繁体中文", @"繁體中文") ];
	for (NSInteger i = 0; i < 2; i++) {
		NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:scripts[i]
			action:@selector(chooseScript:) keyEquivalent:@""];
		item.tag = i;
		item.state = script == i
			? NSControlStateValueOn : NSControlStateValueOff;
		if (script != i) {
			item.keyEquivalent = @"f";
			item.keyEquivalentModifierMask
				= NSEventModifierFlagControl | NSEventModifierFlagShift;
		}
		[menu addItem:item];
	}
	return menu;
}


// The defaults observer applies it, as it does a change in the settings.
- (void)chooseScript:(id)sender
{
	// IMK hands a menu action a dictionary holding the item.
	id item = sender;
	if ([sender isKindOfClass:NSDictionary.class])
		item = [(NSDictionary*)sender objectForKey:kIMKCommandMenuItemName];
	if (![item isKindOfClass:NSMenuItem.class])
		return;
	NSInteger script = [(NSMenuItem*)item tag];
	HPDebug(@"menu: script %ld", long(script));
	[NSUserDefaults.standardUserDefaults setInteger:script forKey:@"script"];
}


- (void)toggleFromMenu:(id)sender
{
	if (!sLoaded)
		return;
	[self claim];
	HPDebug(@"menu: %s", sEngine->Chinese() ? "英" : "中");
	[self toggleChinese];
}


- (void)showPreferences:(id)sender
{
	HPShowSettings();
}


- (void)checkForUpdates:(id)sender
{
	HPShowSettings();
	HPUpdaterCheck();
}


// #pragma mark - AI


// The text before the cursor, for clients that say (many terminals don't:
// the engine then goes by what was typed here).
- (void)readContext
{
	id<IMKTextInput> client = self.textClient;
	NSRange selection = [client selectedRange];
	if (selection.location == NSNotFound)
		return;
	NSUInteger start = selection.location > kContextLength
		? selection.location - kContextLength : 0;
	NSAttributedString* text = [client attributedSubstringFromRange:
		NSMakeRange(start, selection.location - start)];
	if (text != nil)
		sEngine->SetContextText(text.string.UTF8String);
}


// As with the cloud: the engine says what to ask, the helper answers on the
// side, and the engine takes the answer if it still fits. Which candidate
// reads best after the text, as the keys change.
- (void)askAIPick
{
	if (!HPAIReady() || ![NSUserDefaults.standardUserDefaults boolForKey:@"aiPicks"])
		return;
	haikupinyin::Engine::AIQuery query = sEngine->AIPickQuery();
	if (query.options.empty() || query.revision == sAIPickAsked)
		return;
	sAIPickAsked = query.revision;
	NSMutableArray<NSString*>* options = [NSMutableArray new];
	for (const std::string& option : query.options)
		[options addObject:NS(option)];
	HPAIRank(NS(query.prompt), options, ^(NSArray<NSNumber*>* answer) {
		std::vector<double> scores;
		for (NSNumber* score : answer)
			scores.push_back(score.doubleValue);
		HPInputController* last = sLast;
		bool moved = sLoaded && sEngine->SetAIPickResult(query, scores);
		HPDebug(@"AI pick after %s: %s", query.prompt.c_str(),
			moved ? "moved" : "kept");
		if (moved && last != nil)
			[last showCandidates];
	});
}


// What likely comes after a commit.
- (void)askAICompletion
{
	if (!HPAIReady()
		|| ![NSUserDefaults.standardUserDefaults boolForKey:@"aiComplete"]
		|| [self textAfterCursor]) {
		return;
	}
	std::string prompt = sEngine->AICompletionQuery();
	if (prompt.empty() || prompt == sAICompletionAsked)
		return;
	sAICompletionAsked = prompt;
	HPAIComplete(NS(prompt), kAIThreshold, ^(NSString* text) {
		HPInputController* last = sLast;
		if (sLoaded && sEngine->SetAICompletion(prompt, text.UTF8String)
			&& last != nil) {
			HPDebug(@"AI completion after %s: %@", prompt.c_str(), text);
			[last sync];
		}
	});
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
