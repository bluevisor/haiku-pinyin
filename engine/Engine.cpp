#include "Engine.h"

#include <algorithm>

namespace haikupinyin {

static const size_t kLongestInput = 64;
static const size_t kCandidateLimit = 600;
static const int kSaveEvery = 5;	// commits between user dictionary saves


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
	fDecoder(fLexicon, fUser)
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
	// Shortcuts belong to the application, but not halfway through a word.
	if ((modifiers & (kControl | kCommand)) != 0)
		return Composing();
	if (!fChinese)
		return false;

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
	fDecoder.Decode(fGraph);
	fCandidates = fDecoder.Candidates(kCandidateLimit);
	fSpelled = fDecoder.Spelled(fGraph);
	fHighlight = 0;
}


void
Engine::_Select(const Candidate& candidate)
{
	Pick pick{candidate.text, candidate.consumed, candidate.key,
		candidate.cost};
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
	Reset();
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
