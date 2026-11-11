#include "Engine.h"
#include "VMode.h"
#include "Words.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

namespace haikupinyin {

static const size_t kLongestInput = 64;
static const size_t kCandidateLimit = 600;
static const int kSaveEvery = 5;	// commits between user dictionary saves
static const size_t kEmojiReach = 5;	// candidates checked for emoji


// The first or last character of UTF-8 text.
static std::string
EdgeCharacter(const std::string& text, bool last)
{
	if (text.empty())
		return text;
	if (!last) {
		size_t end = 1;
		while (end < text.size()
			&& (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) {
			end++;
		}
		return text.substr(0, end);
	}
	size_t start = text.size() - 1;
	while (start > 0
		&& (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80) {
		start--;
	}
	return text.substr(start);
}


Engine::Engine()
	:
	fDecoder(fLexicon, fUser, fBigram)
{
}


bool
Engine::Load(const std::string& directory, std::string* error)
{
	if (!fLexicon.Load(directory + "/pinyin.dict", error))
		return false;
	if (!fShuangpin.Load(directory + "/shuangpin.txt",
			fLexicon.GetSyllables(), error)) {
		return false;
	}
	// Without these, traditional output falls back to simplified.
	fToTraditional.Load(directory + "/s2t.txt");
	fToTaiwan.Load(directory + "/tw.txt");
	fToHongKong.Load(directory + "/hk.txt");

	fBigram.Load(directory);
	std::ifstream english(directory + "/english.txt");
	std::string word;
	while (std::getline(english, word)) {
		std::string lower = word;
		for (char& c : lower)
			c = char(tolower(c));
		fEnglish.push_back(word);
		fEnglishLower.push_back(lower);
	}

	std::ifstream emoji(directory + "/emoji.txt");
	std::string line;
	while (std::getline(emoji, line)) {
		size_t tab = line.find('\t');
		if (tab == std::string::npos)
			continue;
		std::istringstream values(line.substr(tab + 1));
		std::vector<std::string>& list = fEmoji[line.substr(0, tab)];
		std::string value;
		while (values >> value)
			list.push_back(value);
	}
	return true;
}


void
Engine::SetUserDictionary(const std::string& path)
{
	fUserPath = path;
	fUser.Load(path, fLexicon.GetSyllables());
	fUserStamp = FileStamp::Of(path);
	size_t slash = path.rfind('/');
	fSyncStamp = FileStamp();
	SetPhrasesFile((slash == std::string::npos ? std::string()
		: path.substr(0, slash + 1)) + "phrases.txt");
}


void
Engine::SetPhrasesFile(const std::string& path)
{
	fPhrases.SetPath(path);
	_ConfigureSync();
}


// As a composition starts: the files the user may have changed meanwhile.
void
Engine::_Refresh()
{
	fPhrases.Refresh();
	_RefreshUser();
	_RefreshSync();
}


void
Engine::SetSyncFolder(const std::string& folder)
{
	fSyncFixed = true;
	fSyncFolder = folder;
	_ConfigureSync();
}


// The sync-folder file, when it changed: a stat.
void
Engine::_ConfigureSync()
{
	if (!fSyncFixed && !fUserPath.empty()) {
		FileStamp stamp = FileStamp::Of(SyncSettingFile(fUserPath));
		if (stamp != fSyncStamp) {
			fSyncStamp = stamp;
			fSyncFolder = ReadSyncSetting(fUserPath);
		}
	}
	fSync.SetFolder(fUserPath.empty() ? std::string() : fSyncFolder,
		fUserPath, fPhrases.Path());
}


// Whatever the worker brought, joined in; it's asked to look again.
void
Engine::_RefreshSync()
{
	_ConfigureSync();
	if (!fSync.On())
		return;
	fSync.Poke();
	std::vector<SyncedWord> words;
	if (fSync.Take(words))
		_JoinSynced(words);
}


int
Engine::_JoinSynced(const std::vector<SyncedWord>& words)
{
	const Syllables& syllables = fLexicon.GetSyllables();
	UserDict theirs;
	for (const SyncedWord& word : words) {
		std::vector<SyllableId> key;
		std::istringstream names(word.pinyin);
		std::string name;
		bool valid = true;
		while (valid && names >> name) {
			int id = syllables.Find(name);
			valid = id >= 0;
			key.push_back(SyllableId(id));
		}
		if (valid)
			theirs.Add(key, word.text, word.cost, word.count, word.time);
	}
	return fUser.Join(theirs);
}


SyncReport
Engine::SyncNow()
{
	SyncReport report;
	_ConfigureSync();
	if (!fSync.On()) {
		report.error = "no sync folder";
		return report;
	}
	_RefreshUser();
	std::vector<SyncedWord> words;
	fSync.Read(words, report);
	if (!report.error.empty())
		return report;
	report.changed = _JoinSynced(words);
	fPhrases.Refresh();
	if (!SaveUserDictionary()) {
		report.error = "can't save " + fUserPath;
		return report;
	}
	fSync.Write(report);
	return report;
}


// The user dictionary changed on disk (an import, hpcli): read it again,
// keeping what was learned here since the last save.
void
Engine::_RefreshUser()
{
	if (fUserPath.empty())
		return;
	FileStamp stamp = FileStamp::Of(fUserPath);
	if (stamp == fUserStamp)
		return;
	fUserStamp = stamp;
	UserDict disk;
	if (!disk.Load(fUserPath, fLexicon.GetSyllables()))
		return;
	if (fUser.Dirty())
		disk.Join(fUser);
	fUser = std::move(disk);
}


bool
Engine::SaveUserDictionary()
{
	fUnsaved = 0;
	if (fUserPath.empty())
		return true;
	_RefreshUser();
	if (!fUser.Dirty())
		return true;
	if (!fUser.Save(fUserPath, fLexicon.GetSyllables()))
		return false;
	fUserStamp = FileStamp::Of(fUserPath);
	fSync.Wake();
	return true;
}


bool
Engine::ExportWords(const std::string& path, int* count)
{
	_RefreshUser();
	return WriteWordFile(path, fUser, fLexicon.GetSyllables(), count);
}


Engine::ImportResult
Engine::ImportWords(const std::string& path)
{
	ImportResult result;
	std::ifstream input(path, std::ios::binary);
	std::ostringstream contents;
	if (!input) {
		result.problem = ImportProblem::kUnreadable;
		return result;
	}
	contents << input.rdbuf();
	_RefreshUser();
	WordFile file = ReadWordFile(contents.str(), fLexicon.GetSyllables());
	result.format = file.format;
	result.skipped = file.skipped;
	result.problem = file.problem;
	if (result.problem != ImportProblem::kNone)
		return result;
	for (const ResolvedWord& word
			: ResolveWords(fLexicon, file.words, &result.skipped)) {
		fUser.Merge(word.key, word.text, word.cost, word.count);
		result.words++;
	}

	// Phrases go at the end of the phrases file, unless they're there.
	fPhrases.Refresh();
	std::vector<Phrases::Phrase> added;
	std::set<std::string> known;
	for (const Phrases::Phrase& phrase : file.phrases) {
		bool there = !known.insert(phrase.code + '\t' + phrase.text).second;
		for (const Phrases::Phrase& existing : fPhrases.Find(phrase.code))
			there = there || existing.text == phrase.text;
		if (!there)
			added.push_back(phrase);
	}
	if (!added.empty() && !fPhrases.Append(added))
		result.skipped += int(file.phrases.size());
	else
		result.phrases = int(file.phrases.size());

	if (!SaveUserDictionary()) {
		result.problem = ImportProblem::kNotSaved;
		result.words = 0;
	}
	return result;
}


std::string
Engine::ImportResult::Summary() const
{
	switch (problem) {
		case ImportProblem::kNone:
			break;
		case ImportProblem::kUnreadable:
			return "没能导入：读不了这个文件。";
		case ImportProblem::kNotUtf8:
			return "没能导入：不是 UTF-8 文本。GBK 的话，先用 iconv -f GBK -t UTF-8 转换。";
		case ImportProblem::kBinaryPlist:
			return "没能导入：这是二进制的 plist，先用 plutil -convert xml1 转成文本。";
		case ImportProblem::kNotSaved:
			return "没能导入：存不了学到的词。";
	}
	if (format.empty() || (words == 0 && phrases == 0))
		return "文件里没有能导入的词或短语。";
	static const struct { const char* format; const char* name; } kNames[] = {
		{"Haiku Pinyin", "海酷拼音"},
		{"Sogou", "搜狗词库"},
		{"Sogou custom phrases", "搜狗自定义短语"},
		{"Rime user dictionary", "Rime 用户词典"},
		{"Rime custom phrases", "Rime 自定义短语"},
		{"text replacements", "Mac 文本替换"},
		{"mixed", "几种格式"},
	};
	std::string name = format;
	for (const auto& known : kNames) {
		if (format == known.format)
			name = known.name;
	}
	std::string summary = "导入了";
	if (words > 0)
		summary += " " + std::to_string(words) + " 个词";
	if (words > 0 && phrases > 0)
		summary += "、";
	if (phrases > 0)
		summary += " " + std::to_string(phrases) + " 条短语";
	summary += "（" + name + "）";
	if (skipped > 0)
		summary += "，跳过 " + std::to_string(skipped) + " 条";
	return summary + "。";
}


std::vector<std::string>
Engine::Layouts()
{
	return {"full", "pyjj", "xiaohe", "ziranma", "mspy", "abc"};
}


void
Engine::SetOptions(const Options& options)
{
	Reset();
	fOptions = options;
	if (fOptions.layout != "full" && !fShuangpin.HasLayout(fOptions.layout))
		fOptions.layout = "full";
	fOptions.pageSize = std::clamp(fOptions.pageSize, 3, 10);
}


void
Engine::SetChinese(bool chinese)
{
	// Switching away mid-word keeps what was typed, as letters.
	if (!chinese && Composing())
		_Finish(_Remaining());
	fChinese = chinese;
	fSuggestion.clear();
}


bool
Engine::ProcessKey(Key key, char32_t character, uint32_t modifiers)
{
	if (!Loaded())
		return false;
	// A completion on offer: Tab takes it, any other key drops it.
	if (!fSuggestion.empty()) {
		std::string suggestion;
		suggestion.swap(fSuggestion);
		if (key == Key::kTab && modifiers == 0 && !Composing()) {
			_Commit(_Convert(suggestion), suggestion);
			SetContextText(fContextText);
			return true;
		}
	}
	bool composing = Composing();
	bool used = _ProcessKey(key, character, modifiers);
	if (!used && !composing && !Composing()) {
		// Typed through: a character still reads before the cursor; Return,
		// arrows and shortcuts mean somewhere new.
		if (key == Key::kCharacter && character >= 0x20 && character < 0x80
			&& (modifiers & (kControl | kCommand | kOption)) == 0) {
			_AddContext(std::string(1, char(character)));
		} else
			fContextText.clear();
	}
	return used;
}


bool
Engine::_ProcessKey(Key key, char32_t character, uint32_t modifiers)
{
	// Ctrl or Shift + Delete forgets a word it learned from you.
	if (Composing() && key == Key::kDelete
		&& (modifiers & (kControl | kShift)) != 0) {
		const Candidate& candidate = fCandidates.empty()
			? Candidate{} : fCandidates[fHighlight];
		if (!candidate.key.empty()
			&& fUser.Forget(candidate.key, candidate.text)) {
			SaveUserDictionary();
			_Update();
		}
		return true;
	}
	// Shortcuts belong to the application, but not halfway through a word.
	if ((modifiers & (kControl | kCommand)) != 0)
		return Composing();
	if (!fChinese)
		return false;

	if (Composing()) {
		bool used = _Compose(key, character);
		// Digits pick candidates here; they never reach the text.
		bool digit = character >= '0' && character <= '9';
		fLastCharacter = key == Key::kCharacter && !digit ? character : 0;
		return used;
	}

	if (key != Key::kCharacter || (modifiers & kOption) != 0) {
		fLastCharacter = 0;
		return false;
	}
	// v-mode: v starts no pinyin syllable, but it's a key in shuangpin, so
	// there it's Shift+V.
	bool full = fOptions.layout == "full";
	if ((character == 'v' && full) || (character == 'V' && !full)) {
		_Refresh();
		fKeys = "v";
		fUsed = 0;
		fVMode = true;
		_Update();
		return true;
	}
	if (character >= 'a' && character <= 'z') {
		_Refresh();
		fKeys = std::string(1, char(character));
		fUsed = 0;
		_Update();
		return true;
	}
	std::string punctuation = _Punctuation(character);
	fLastCharacter = character;
	if (punctuation.empty())
		return false;
	_Commit(punctuation, punctuation);
	return true;
}


bool
Engine::_Compose(Key key, char32_t character)
{
	switch (key) {
		case Key::kCharacter:
			break;
		case Key::kSpace:
			if (!fCandidates.empty())
				_Select(fCandidates[fHighlight]);
			else if (fVMode)
				_Finish(fKeys);
			return true;
		case Key::kReturn:
		{
			// The keys as typed, the way 搜狗 and macOS do it.
			std::string typed = _Remaining();
			typed.erase(std::remove(typed.begin(), typed.end(), '\''),
				typed.end());
			_Finish(typed);
			return true;
		}
		case Key::kBackspace:
			if (fKeys.size() > fUsed)
				fKeys.pop_back();
			if (fKeys.size() == fUsed && !fPicks.empty()) {
				// Back into the last pick: its keys come back.
				fUsed -= fPicks.back().consumed;
				fPicks.pop_back();
			}
			if (fKeys.empty())
				Reset();
			else
				_Update();
			return true;
		case Key::kEscape:
			Reset();
			return true;
		case Key::kLeft:
		case Key::kUp:
			_Move(-1);
			return true;
		case Key::kRight:
		case Key::kDown:
			_Move(1);
			return true;
		case Key::kPageUp:
			if (HasPreviousPage())
				fHighlight = (Page() - 1) * fOptions.pageSize;
			return true;
		case Key::kPageDown:
		case Key::kTab:
			if (HasNextPage())
				fHighlight = (Page() + 1) * fOptions.pageSize;
			return true;
		default:
			return true;
	}

	bool used;
	if (fVMode && _ComposeV(character, &used))
		return used;
	if (character >= 'A' && character <= 'Z')
		character += 'a' - 'A';
	if (_IsLayoutKey(character)) {
		if (fKeys.size() < kLongestInput) {
			fKeys += char(character);
			_Update();
		}
		return true;
	}
	if (character == '\'') {
		if (fKeys.size() > fUsed && fKeys.back() != '\'') {
			fKeys += '\'';
			_Update();
		}
		return true;
	}
	if (character >= '1' && character <= '9') {
		SelectOnPage(int(character - '1'));
		return true;
	}
	if (character == '0') {
		SelectOnPage(9);
		return true;
	}
	if (character == '-') {
		return _Compose(Key::kPageUp, 0);
	}
	if (character == '=') {
		return _Compose(Key::kPageDown, 0);
	}
	if ((character == '[' || character == ']') && !fCandidates.empty()) {
		// 以词定字: just the first or last character of the candidate.
		Candidate candidate = fCandidates[fHighlight];
		bool last = character == ']';
		candidate.text = EdgeCharacter(candidate.text, last);
		if (!candidate.key.empty())
			candidate.key = {last ? candidate.key.back() : candidate.key[0]};
		_Select(candidate);
		return true;
	}

	// Anything else ends the word with the best sentence, then counts as
	// itself.
	std::string punctuation = _Punctuation(character);
	if (!fCandidates.empty())
		_Select(fCandidates[0]);
	if (Composing())
		_Finish(_Remaining());
	if (punctuation.empty())
		return false;
	_Commit(punctuation, punctuation);
	return true;
}


bool
Engine::_IsLayoutKey(char32_t character) const
{
	if (character >= 'a' && character <= 'z')
		return true;
	return character == ';' && fOptions.layout != "full"
		&& fShuangpin.IsKey(fOptions.layout, ';');
}


// v-mode, once there's a number: digits, + - * / ( ) . % are typing, and
// letters pick. True when it took the key.
bool
Engine::_ComposeV(char32_t character, bool* used)
{
	std::string body = fKeys.substr(1);
	bool digit = character >= '0' && character <= '9';
	if (!VNumeric(body) && !(body.empty() && (digit || character == '(')))
		return false;
	*used = true;
	if (digit || (character != 0 && strchr("+-*/().%", int(character))
			!= nullptr)) {
		if (fKeys.size() < kLongestInput) {
			fKeys += char(character);
			_Update();
		}
		return true;
	}
	std::string keys = PickKeys();
	size_t index = keys.find(char(character));
	if (character < 0x80 && index != std::string::npos) {
		SelectOnPage(int(index));
		return true;
	}
	if (character >= 'a' && character <= 'z')
		return true;	// not a pick on this page
	return false;
}


std::string
Engine::PickKeys() const
{
	std::string keys = fVMode && VNumeric(fKeys.substr(1))
		? "abcdefghij" : "1234567890";
	return keys.substr(0, size_t(fOptions.pageSize));
}


// The candidates v-mode has for the keys; false when it has none and they
// read as pinyin after all (very, video).
bool
Engine::_UpdateV()
{
	std::string body = fKeys.substr(1);
	std::vector<std::string> texts = VCandidates(body,
		fNow != 0 ? fNow : time(nullptr));
	if (texts.empty() && !VNumeric(body) && fOptions.layout == "full")
		return false;
	fCandidates.clear();
	int consumed = int(fKeys.size());
	for (const std::string& text : texts)
		fCandidates.push_back(Candidate{text, consumed, 0, {}, false, ""});
	_PlacePhrases();
	fSpelled = fKeys;
	fHighlight = 0;
	return true;
}


void
Engine::_Update()
{
	fRevision++;
	if (fVMode && _UpdateV())
		return;
	std::string remaining = _Remaining();
	if (fOptions.layout == "full") {
		FullPinyin::Parse(remaining, fLexicon.GetSyllables(), fGraph);
	} else {
		fShuangpin.Parse(fOptions.layout, fOptions.vowels, remaining,
			fLexicon.GetSyllables(), fGraph);
	}
	AddFuzzy(fGraph, fLexicon.GetSyllables(), fOptions.fuzzy);
	// Only the first pick of a composition follows the last commit.
	fDecoder.SetContext(fPicks.empty() ? fContext
		: Bigram::Hash(fPicks.back().tail));
	fDecoder.Decode(fGraph);
	fCandidates = fDecoder.Candidates(fGraph, kCandidateLimit);
	_AddEnglish();
	_AddEmoji();
	_PlacePhrases();
	fSpelled = fDecoder.Spelled(fGraph);
	fHighlight = 0;
}


// Emoji go right after the word they stand for, among the first few
// candidates. They have no key, so picking one teaches nothing.
void
Engine::_AddEmoji()
{
	if (!fOptions.emoji || fEmoji.empty())
		return;
	for (size_t i = 0; i < fCandidates.size() && i < kEmojiReach; i++) {
		auto found = fEmoji.find(fCandidates[i].text);
		if (found == fEmoji.end())
			continue;
		Candidate word = fCandidates[i];
		size_t at = i + 1;
		for (const std::string& emoji : found->second) {
			bool shown = false;
			for (const Candidate& candidate : fCandidates)
				shown = shown || candidate.text == emoji;
			if (shown)
				continue;
			fCandidates.insert(fCandidates.begin() + at++,
				Candidate{emoji, word.consumed, word.cost, {}, false, ""});
		}
		i = at - 1;
	}
}


// Custom phrases for exactly the keys typed go at their positions, in place
// of the same text anywhere else. Again after the cloud moves things.
void
Engine::_PlacePhrases()
{
	fCandidates.erase(std::remove_if(fCandidates.begin(), fCandidates.end(),
		[](const Candidate& candidate) { return candidate.phrase; }),
		fCandidates.end());
	if (!fPicks.empty() || fPhrases.Size() == 0)
		return;
	std::vector<Phrases::Phrase> phrases = fPhrases.Find(fKeys);
	for (const Phrases::Phrase& phrase : phrases) {
		fCandidates.erase(std::remove_if(fCandidates.begin(),
			fCandidates.end(), [&](const Candidate& candidate) {
				return candidate.text == phrase.text;
			}), fCandidates.end());
	}
	for (const Phrases::Phrase& phrase : phrases) {
		Candidate candidate{phrase.text, int(fKeys.size()), 0, {}, false, ""};
		candidate.phrase = true;
		size_t at = std::min(size_t(phrase.position - 1), fCandidates.size());
		fCandidates.insert(fCandidates.begin() + at, candidate);
	}
}


void
Engine::_Select(const Candidate& candidate)
{
	Pick pick{candidate.text, candidate.consumed, candidate.key,
		candidate.cost, candidate.tail, candidate.phrase};
	// A separator right after the pick belongs to it.
	while (fUsed + pick.consumed < fKeys.size()
		&& fKeys[fUsed + pick.consumed] == '\'') {
		pick.consumed++;
	}
	fPicks.push_back(std::move(pick));
	fUsed += fPicks.back().consumed;
	if (fUsed >= fKeys.size())
		_Finish("");
	else
		_Update();
}


void
Engine::_Finish(const std::string& tail)
{
	std::string text, shown, run;
	std::vector<SyllableId> key;
	float cost = 0;
	bool learnable = !fPicks.empty();
	for (const Pick& pick : fPicks) {
		text += pick.text;
		// Custom phrases go as written, never converted.
		if (pick.phrase) {
			shown += _Convert(run) + pick.text;
			run.clear();
		} else
			run += pick.text;
		key.insert(key.end(), pick.key.begin(), pick.key.end());
		cost += pick.cost;
		if (pick.key.empty())
			learnable = false;
		else
			fUser.Learn(pick.key, pick.text, pick.cost);
	}
	// Several picks in a row make a phrase worth remembering whole.
	if (learnable && fPicks.size() > 1)
		fUser.Learn(key, text, cost);
	if (!fPicks.empty() && ++fUnsaved >= kSaveEvery)
		SaveUserDictionary();

	_Commit(shown + _Convert(run) + tail, text + tail);
	std::string last = fPicks.empty() || !tail.empty() ? std::string()
		: fPicks.back().tail;
	Reset();
	// The last word typed is the context for the next.
	fContext = last.empty() ? 0 : Bigram::Hash(last);
}


void
Engine::ClearContext()
{
	fContext = 0;
	fContextText.clear();
	fSuggestion.clear();
}


void
Engine::_Commit(const std::string& shown, const std::string& context)
{
	fCommit += shown;
	_AddContext(context);
}


// Keeps the last kContextLength characters.
void
Engine::_AddContext(const std::string& text)
{
	static const size_t kContextLength = 64;
	fContextText += text;
	size_t characters = 0;
	for (size_t i = fContextText.size(); i-- > 0;) {
		if ((static_cast<unsigned char>(fContextText[i]) & 0xc0) != 0x80
			&& ++characters > kContextLength) {
			fContextText.erase(0, i + 1);
			break;
		}
	}
}


// The text before the cursor: its last word (the longest ending that pairs
// with anything) becomes the context.
void
Engine::SetContextText(const std::string& text)
{
	if (&text != &fContextText) {
		fContextText.clear();
		_AddContext(text);
	}
	fContext = 0;
	std::vector<size_t> starts;
	for (size_t i = 0; i < text.size(); i++) {
		if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80)
			starts.push_back(i);
	}
	static const size_t kLongestWord = 4;
	size_t first = starts.size() > kLongestWord
		? starts.size() - kLongestWord : 0;
	for (size_t from = first; from < starts.size(); from++) {
		uint32_t hash = Bigram::Hash(std::string_view(text).substr(starts[from]));
		if (fBigram.HasFollowers(hash)) {
			fContext = hash;
			return;
		}
	}
}


// Typed letters that make an English word are offered as that word: first
// when the pinyin makes no sense, second when it only reads as pinyin by
// guessing at syllables ("hello"), else third. Unfinished words complete
// only when the pinyin makes no sense.
void
Engine::_AddEnglish()
{
	if (!fOptions.english || fOptions.layout != "full" || fEnglish.empty())
		return;
	std::string typed = _Remaining();
	if (typed.size() < 2 || typed.find('\'') != std::string::npos)
		return;
	bool nonsense = !fCandidates.empty() && fCandidates[0].raw;

	std::vector<std::string> words;
	auto at = std::lower_bound(fEnglishLower.begin(), fEnglishLower.end(),
		typed);
	size_t index = size_t(at - fEnglishLower.begin());
	if (at != fEnglishLower.end() && *at == typed)
		words.push_back(fEnglish[index++]);
	if (nonsense && typed.size() >= 3) {
		std::vector<std::string> completions;
		for (; index < fEnglishLower.size() && completions.size() < 40
				&& fEnglishLower[index].compare(0, typed.size(), typed) == 0;
				index++) {
			completions.push_back(fEnglish[index]);
		}
		std::stable_sort(completions.begin(), completions.end(),
			[](const std::string& a, const std::string& b) {
				return a.size() < b.size();
			});
		for (size_t i = 0; i < completions.size() && i < 3; i++)
			words.push_back(completions[i]);
	}
	if (words.empty())
		return;

	bool guessed = !fCandidates.empty() && fCandidates[0].guessed;
	size_t insert = nonsense ? 0
		: std::min<size_t>(guessed ? 1 : 2, fCandidates.size());
	if (!nonsense)
		words.resize(1);
	for (const std::string& word : words) {
		bool shown = false;
		for (const Candidate& candidate : fCandidates)
			shown = shown || candidate.text == word;
		if (shown)
			continue;
		Candidate english{word, int(typed.size()), 0, {}, false, ""};
		english.english = true;
		fCandidates.insert(fCandidates.begin() + insert++, english);
	}
}


std::string
Engine::CloudQuery() const
{
	if (!Composing() || fVMode || fCandidates.empty() || fCandidates[0].raw
		|| fSpelled.find('/') != std::string::npos
		|| std::count(fSpelled.begin(), fSpelled.end(), '\'') < 1) {
		return std::string();
	}
	std::string query = fSpelled;
	query.erase(std::remove(query.begin(), query.end(), '\''), query.end());
	return query;
}


bool
Engine::SetCloudResult(const std::string& query, const std::string& text)
{
	if (text.empty() || query.empty() || query != CloudQuery())
		return false;
	for (size_t i = 0; i < fCandidates.size(); i++) {
		if (fCandidates[i].text != text)
			continue;
		if (i <= 1)
			return false;	// already where it would go
		fCandidates.erase(fCandidates.begin() + i);
		break;
	}
	const Candidate& best = fCandidates[0];
	Candidate cloud{text, int(fKeys.size() - fUsed), best.cost, {}, true,
		text};
	cloud.cloud = true;
	// Learnable when it has a character per syllable, as it usually does.
	size_t characters = 0;
	for (unsigned char c : text)
		characters += (c & 0xc0) != 0x80;
	if (characters == best.key.size())
		cloud.key = best.key;
	fCandidates.insert(fCandidates.begin() + 1, cloud);
	_PlacePhrases();
	fRevision++;
	return true;
}


int
Engine::CloudIndexOnPage() const
{
	size_t first = size_t(Page()) * fOptions.pageSize;
	for (size_t i = first;
			i < fCandidates.size() && i < first + fOptions.pageSize; i++) {
		if (fCandidates[i].cloud)
			return int(i - first);
	}
	return -1;
}


// How much likelier (ln) the AI must find a candidate to move it first: the
// engine's order is right most of the time.
static const double kAIMargin = 1.0;


Engine::AIQuery
Engine::AIPickQuery() const
{
	AIQuery query;
	if (!Composing() || fVMode || fHighlight != 0 || fCandidates.empty()
		|| fCandidates[0].ai) {
		return query;
	}
	int remaining = int(fKeys.size() - fUsed);
	for (size_t i = 0; i < fCandidates.size() && query.indexes.size() < 5; i++) {
		const Candidate& candidate = fCandidates[i];
		if (candidate.consumed == remaining && !candidate.raw
			&& !candidate.english && !candidate.key.empty()) {
			query.indexes.push_back(int(i));
			query.options.push_back(candidate.text);
		}
	}
	if (query.indexes.size() < 2 || query.indexes[0] != 0)
		return AIQuery();
	query.prompt = fContextText;
	for (const Pick& pick : fPicks)
		query.prompt += pick.text;
	query.revision = fRevision;
	return query;
}


bool
Engine::SetAIPickResult(const AIQuery& query, const std::vector<double>& scores)
{
	if (query.revision != fRevision || fHighlight != 0 || scores.empty()
		|| scores.size() != query.indexes.size()) {
		return false;
	}
	size_t best = std::max_element(scores.begin(), scores.end()) - scores.begin();
	if (best == 0 || scores[best] - scores[0] < kAIMargin)
		return false;
	Candidate chosen = fCandidates[query.indexes[best]];
	chosen.ai = true;
	fCandidates.erase(fCandidates.begin() + query.indexes[best]);
	fCandidates.insert(fCandidates.begin(), chosen);
	fRevision++;
	return true;
}


int
Engine::AIIndexOnPage() const
{
	size_t first = size_t(Page()) * fOptions.pageSize;
	for (size_t i = first;
			i < fCandidates.size() && i < first + fOptions.pageSize; i++) {
		if (fCandidates[i].ai)
			return int(i - first);
	}
	return -1;
}


// Whether text ends with a Chinese character: what a completion follows.
static bool
EndsWithHanzi(const std::string& text)
{
	size_t start = text.size();
	while (start > 0 && (static_cast<unsigned char>(text[start - 1]) & 0xc0) == 0x80)
		start--;
	if (start == 0)
		return false;
	start--;
	const unsigned char* bytes
		= reinterpret_cast<const unsigned char*>(text.data()) + start;
	char32_t c;
	size_t length = text.size() - start;
	if (length == 3 && (bytes[0] & 0xf0) == 0xe0)
		c = (bytes[0] & 0x0f) << 12 | (bytes[1] & 0x3f) << 6 | (bytes[2] & 0x3f);
	else if (length == 4 && (bytes[0] & 0xf8) == 0xf0)
		c = (bytes[0] & 0x07) << 18 | (bytes[1] & 0x3f) << 12
			| (bytes[2] & 0x3f) << 6 | (bytes[3] & 0x3f);
	else
		return false;
	return (c >= 0x3400 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff)
		|| (c >= 0x20000 && c <= 0x3ffff);
}


std::string
Engine::AICompletionQuery() const
{
	if (Composing() || !fChinese || !fSuggestion.empty()
		|| !EndsWithHanzi(fContextText)) {
		return std::string();
	}
	return fContextText;
}


bool
Engine::SetAICompletion(const std::string& prompt, const std::string& text)
{
	if (text.empty() || prompt.empty() || prompt != AICompletionQuery())
		return false;
	fSuggestion = text;
	return true;
}


std::string
Engine::Suggestion() const
{
	return fSuggestion.empty() ? std::string() : _Convert(fSuggestion);
}


void
Engine::_Move(int delta)
{
	if (fCandidates.empty())
		return;
	fHighlight = std::clamp(fHighlight + delta, 0,
		int(fCandidates.size()) - 1);
}


std::string
Engine::_Punctuation(char32_t character)
{
	if (!fOptions.fullwidthPunctuation)
		return std::string();
	bool traditional = fOptions.script != Script::kSimplified;
	bool afterDigit = fLastCharacter >= '0' && fLastCharacter <= '9';
	switch (character) {
		case ',': return afterDigit ? "" : "，";
		case '.': return afterDigit ? "" : "。";
		case '?': return "？";
		case '!': return "！";
		case ':': return afterDigit ? "" : "：";
		case ';': return "；";
		case '(': return "（";
		case ')': return "）";
		case '[': return "【";
		case ']': return "】";
		case '<': return "《";
		case '>': return "》";
		case '\\': return "、";
		case '^': return "……";
		case '_': return "——";
		case '$': return "￥";
		case '~': return "～";
		case '`': return "·";
		case '"':
			fDoubleQuoteOpen = !fDoubleQuoteOpen;
			if (traditional)
				return fDoubleQuoteOpen ? "「" : "」";
			return fDoubleQuoteOpen ? "“" : "”";
		case '\'':
			fSingleQuoteOpen = !fSingleQuoteOpen;
			if (traditional)
				return fSingleQuoteOpen ? "『" : "』";
			return fSingleQuoteOpen ? "‘" : "’";
	}
	return std::string();
}


std::string
Engine::_Convert(const std::string& text) const
{
	switch (fOptions.script) {
		case Script::kSimplified:
			return text;
		case Script::kTraditional:
			return fToTraditional.Convert(text);
		case Script::kTaiwan:
			return fToTaiwan.Convert(fToTraditional.Convert(text));
		case Script::kHongKong:
			return fToHongKong.Convert(fToTraditional.Convert(text));
	}
	return text;
}


std::string
Engine::TakeCommit()
{
	std::string commit;
	commit.swap(fCommit);
	return commit;
}


std::string
Engine::Preedit() const
{
	std::string picked;
	for (const Pick& pick : fPicks)
		picked += pick.text;
	return _Convert(picked) + fSpelled;
}


size_t
Engine::PickedLength() const
{
	std::string picked;
	for (const Pick& pick : fPicks)
		picked += pick.text;
	return _Convert(picked).size();
}


std::vector<std::string>
Engine::PageCandidates() const
{
	std::vector<std::string> page;
	size_t first = size_t(Page()) * fOptions.pageSize;
	for (size_t i = first;
			i < fCandidates.size() && i < first + fOptions.pageSize; i++) {
		const Candidate& candidate = fCandidates[i];
		page.push_back(candidate.phrase ? PhraseLabel(candidate.text)
			: _Convert(candidate.text));
	}
	return page;
}


// A custom phrase as the candidate window shows it: on one line, ⏎ for
// its line breaks, and cut short when it's long.
std::string
Engine::PhraseLabel(const std::string& text)
{
	static const size_t kLongest = 24;	// characters
	std::string label;
	size_t characters = 0;
	for (size_t i = 0; i < text.size(); i++) {
		unsigned char c = text[i];
		if ((c & 0xc0) != 0x80 && ++characters > kLongest) {
			label += "…";
			break;
		}
		if (c == '\n')
			label += "⏎";
		else if (c != '\r')
			label += char(c);
	}
	return label;
}


bool
Engine::HasNextPage() const
{
	return size_t(Page() + 1) * fOptions.pageSize < fCandidates.size();
}


bool
Engine::SelectOnPage(int index)
{
	size_t at = size_t(Page()) * fOptions.pageSize + index;
	if (index < 0 || index >= fOptions.pageSize || at >= fCandidates.size())
		return false;
	_Select(fCandidates[at]);
	return true;
}


void
Engine::Reset()
{
	fKeys.clear();
	fUsed = 0;
	fPicks.clear();
	fCandidates.clear();
	fSpelled.clear();
	fHighlight = 0;
	fVMode = false;
}

}	// namespace haikupinyin
