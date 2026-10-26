#include "Engine.h"

#include <algorithm>
#include <cstring>
#include <fstream>
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
}


bool
Engine::SaveUserDictionary()
{
	fUnsaved = 0;
	if (fUserPath.empty() || !fUser.Dirty())
		return true;
	return fUser.Save(fUserPath, fLexicon.GetSyllables());
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
}


bool
Engine::ProcessKey(Key key, char32_t character, uint32_t modifiers)
{
	if (!Loaded())
		return false;
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
	if ((modifiers & (kControl | kCommand)) != 0) {
		fPredictions.clear();
		return Composing();
	}
	if (!fChinese)
		return false;

	if (!Composing() && !fPredictions.empty()) {
		int count = std::min(int(fPredictions.size()), fOptions.pageSize);
		if (key == Key::kCharacter && character >= '1'
			&& character < char32_t('1' + count)) {
			return SelectOnPage(int(character - '1'));
		}
		fPredictions.clear();
		if (key == Key::kEscape)
			return true;
	}

	if (Composing()) {
		bool used = _Compose(key, character);
		fLastCharacter = key == Key::kCharacter ? character : 0;
		return used;
	}

	if (key != Key::kCharacter || (modifiers & kOption) != 0) {
		fLastCharacter = 0;
		return false;
	}
	if (character >= 'a' && character <= 'z') {
		fKeys = std::string(1, char(character));
		fUsed = 0;
		_Update();
		return true;
	}
	std::string punctuation = _Punctuation(character);
	fLastCharacter = character;
	if (punctuation.empty())
		return false;
	fCommit += punctuation;
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
	fCommit += punctuation;
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


void
Engine::_Update()
{
	std::string remaining = _Remaining();
	if (fOptions.layout == "full") {
		FullPinyin::Parse(remaining, fLexicon.GetSyllables(), fGraph);
	} else {
		fShuangpin.Parse(fOptions.layout, remaining, fLexicon.GetSyllables(),
			fGraph);
	}
	AddFuzzy(fGraph, fLexicon.GetSyllables(), fOptions.fuzzy);
	// Only the first pick of a composition follows the last commit.
	fDecoder.SetContext(fPicks.empty() ? fContext
		: Bigram::Hash(fPicks.back().tail));
	fDecoder.Decode(fGraph);
	fCandidates = fDecoder.Candidates(fGraph, kCandidateLimit);
	_AddEnglish();
	_AddEmoji();
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


void
Engine::_Select(const Candidate& candidate)
{
	Pick pick{candidate.text, candidate.consumed, candidate.key,
		candidate.cost, candidate.tail};
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
	std::string text;
	std::vector<SyllableId> key;
	float cost = 0;
	bool learnable = !fPicks.empty();
	for (const Pick& pick : fPicks) {
		text += pick.text;
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

	fCommit += _Convert(text) + tail;
	std::string last = fPicks.empty() || !tail.empty() ? std::string()
		: fPicks.back().tail;
	Reset();
	if (last.empty())
		ClearContext();
	else
		_Predict(last, learnable ? text : std::string(), key);
}


// The last word typed becomes the context, and its likely followers the
// 联想 shown next.
void
Engine::_Predict(const std::string& word, const std::string& text,
	const std::vector<SyllableId>& key)
{
	fContext = Bigram::Hash(word);
	fPredictions.clear();
	if (!fOptions.prediction)
		return;
	size_t limit = size_t(fOptions.pageSize);
	// Phrases what was typed begins come first, then words that tend to
	// follow the last one.
	fPredictions = _Continuations(text, key, limit);
	for (const std::string& next : fBigram.Predict(fContext, limit * 2)) {
		if (fPredictions.size() >= limit)
			break;
		if (std::find(fPredictions.begin(), fPredictions.end(), next)
				== fPredictions.end()) {
			fPredictions.push_back(next);
		}
	}
}


// Words that start with 的 or end in a particle make poor 联想: 的人, 你啊.
static bool
IsParticleBound(const std::string& word)
{
	static const char* const kParticles[] = {
		"的", "了", "啊", "吗", "呢", "吧"
	};
	if (word.compare(0, 3, "的") == 0)
		return true;
	for (const char* particle : kParticles) {
		size_t size = strlen(particle);
		if (word.size() >= size
			&& word.compare(word.size() - size, size, particle) == 0) {
			return true;
		}
	}
	return false;
}


// Lexicon phrases that start with the end of text ("祝你生日" begins
// 祝你生日快乐): what they add, longer matches of what was typed first,
// then by how common the phrase is. text has one character per syllable
// of key.
std::vector<std::string>
Engine::_Continuations(const std::string& text,
	const std::vector<SyllableId>& key, size_t limit) const
{
	std::vector<std::string> found;
	std::vector<size_t> starts;
	for (size_t i = 0; i < text.size(); i++) {
		if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80)
			starts.push_back(i);
	}
	if (key.size() < 2 || starts.size() != key.size())
		return found;

	static const size_t kLongestContext = 6;
	// Rarer phrases are mostly noise (你好给力呀); this is rime-ice weight
	// 1000 (生日快乐 is 58750).
	static const float kRarestPhrase = 15.8f;
	size_t first = key.size() > kLongestContext ? key.size() - kLongestContext
		: 0;
	for (size_t from = first; from + 2 <= key.size(); from++) {
		std::string context = text.substr(starts[from]);
		int depth = int(key.size() - from);
		KeyIndex::Range range = fLexicon.All();
		for (int i = 0; i < depth && !range.Empty(); i++)
			range = fLexicon.Narrow(range, i, key[from + i]);
		range.begin = fLexicon.Exact(range, depth).end;	// longer ones only
		std::vector<std::pair<float, std::string>> here;
		for (uint32_t i = range.begin; i < range.end; i++) {
			std::string_view word = fLexicon.Word(i);
			if (fLexicon.Cost(i) > kRarestPhrase || word.size() <= context.size()
				|| word.compare(0, context.size(), context) != 0) {
				continue;
			}
			std::string rest(word.substr(context.size()));
			size_t characters = 0;
			for (unsigned char c : rest)
				characters += (c & 0xc0) != 0x80;
			if (characters < 2 || IsParticleBound(rest))
				continue;
			// Only when what it adds is a word of its own: 快乐, not 给力呀.
			KeyIndex::Range own = fLexicon.All();
			int length = fLexicon.KeyLength(i);
			for (int k = depth; k < length && !own.Empty(); k++)
				own = fLexicon.Narrow(own, k - depth, fLexicon.KeyAt(i, k));
			own = fLexicon.Exact(own, length - depth);
			bool known = false;
			for (uint32_t j = own.begin; j < own.end && !known; j++)
				known = fLexicon.Word(j) == rest;
			if (known)
				here.emplace_back(fLexicon.Cost(i), rest);
		}
		std::sort(here.begin(), here.end());
		for (const auto& [cost, rest] : here) {
			if (found.size() >= limit)
				return found;
			if (std::find(found.begin(), found.end(), rest) == found.end())
				found.push_back(rest);
		}
	}
	return found;
}


void
Engine::ClearContext()
{
	fContext = 0;
	fPredictions.clear();
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
	if (!Composing() || fCandidates.empty() || fCandidates[0].raw
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
	if (Predicting()) {
		for (size_t i = 0; i < fPredictions.size()
				&& int(i) < fOptions.pageSize; i++) {
			page.push_back(_Convert(fPredictions[i]));
		}
		return page;
	}
	size_t first = size_t(Page()) * fOptions.pageSize;
	for (size_t i = first;
			i < fCandidates.size() && i < first + fOptions.pageSize; i++) {
		page.push_back(_Convert(fCandidates[i].text));
	}
	return page;
}


bool
Engine::HasNextPage() const
{
	return size_t(Page() + 1) * fOptions.pageSize < fCandidates.size();
}


bool
Engine::SelectOnPage(int index)
{
	if (Predicting()) {
		if (index < 0 || index >= int(fPredictions.size())
			|| index >= fOptions.pageSize) {
			return false;
		}
		std::string word = fPredictions[index];
		fCommit += _Convert(word);
		_Predict(word);
		return true;
	}
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
}

}	// namespace haikupinyin
