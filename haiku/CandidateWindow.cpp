#include "CandidateWindow.h"

#include <Screen.h>

#include <algorithm>

static const float kPadding = 6.0f;
static const float kGap = 14.0f;		// between candidates
static const float kRowGap = 3.0f;
static const char* const kCloudMark = " ☁";


static rgb_color
Mix(rgb_color a, rgb_color b, float amount)
{
	return make_color(
		uint8(a.red + (b.red - a.red) * amount),
		uint8(a.green + (b.green - a.green) * amount),
		uint8(a.blue + (b.blue - a.blue) * amount));
}


// BFont::StringWidth() doesn't see the fallback font that draws CJK, so it
// comes up short for 中文 and emoji: count wide characters as a full em.
static float
TextWidth(const BFont& font, const char* text)
{
	float width = 0;
	BString narrow;
	for (const unsigned char* c = (const unsigned char*)text; *c != 0;) {
		uint32 code = *c;
		int length = 1;
		if (code >= 0xf0) {
			code &= 0x07;
			length = 4;
		} else if (code >= 0xe0) {
			code &= 0x0f;
			length = 3;
		} else if (code >= 0xc0) {
			code &= 0x1f;
			length = 2;
		}
		for (int i = 1; i < length && c[i] != 0; i++)
			code = (code << 6) | (c[i] & 0x3f);
		if (code == 0xfe0f || code == 0x200d)
			;	// selectors and joiners take no room
		else if (code >= 0x2e80 || (code >= 0x2600 && code < 0x2800))
			width += font.Size();
		else
			narrow.Append((const char*)c, length);
		c += length;
	}
	return width + font.StringWidth(narrow.String());
}


CandidateView::CandidateView(BMessenger target)
	:
	BView(BRect(0, 0, 10, 10), "candidates", B_FOLLOW_ALL,
		B_WILL_DRAW | B_FRAME_EVENTS),
	fTarget(target),
	fHighlight(0),
	fHover(-1),
	fCloud(-1),
	fPrevious(false),
	fNext(false)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	fCandidateFont = *be_plain_font;
	fCandidateFont.SetSize(std::max(15.0f, be_plain_font->Size() * 1.35f));
	fSpelledFont = *be_plain_font;
	fSpelledFont.SetSize(std::max(11.0f, be_plain_font->Size() * 0.95f));
}


void
CandidateView::SetContents(const BMessage& message)
{
	fSpelled = message.GetString("spelled", "");
	fCandidates.clear();
	const char* candidate;
	for (int32 i = 0; message.FindString("candidate", i, &candidate) == B_OK;
			i++) {
		fCandidates.push_back(candidate);
	}
	fHighlight = message.GetInt32("highlight", 0);
	fCloud = message.GetInt32("cloud", -1);
	fHover = -1;
	fPrevious = message.GetBool("previous", false);
	fNext = message.GetBool("next", false);
	_Layout();
	Invalidate();
}


void
CandidateView::_Layout()
{
	font_height spelledHeight, candidateHeight;
	fSpelledFont.GetHeight(&spelledHeight);
	fCandidateFont.GetHeight(&candidateHeight);
	// 联想 has no pinyin to show: candidates only.
	float top = kPadding;
	if (!fSpelled.IsEmpty())
		top += ceilf(spelledHeight.ascent + spelledHeight.descent) + kRowGap;
	float height = ceilf(candidateHeight.ascent + candidateHeight.descent) + 4;

	fFrames.clear();
	float x = kPadding;
	for (size_t i = 0; i < fCandidates.size(); i++) {
		BString label;
		label << (i + 1) << ". " << fCandidates[i];
		if (int32(i) == fCloud)
			label << kCloudMark;
		float width = TextWidth(fCandidateFont, label.String()) + 8;
		fFrames.push_back(BRect(x, top, x + width, top + height));
		x += width + kGap;
	}
}


BSize
CandidateView::ContentSize()
{
	font_height spelledHeight, candidateHeight;
	fSpelledFont.GetHeight(&spelledHeight);
	fCandidateFont.GetHeight(&candidateHeight);
	float width = TextWidth(fSpelledFont, fSpelled.String());
	if (!fFrames.empty())
		width = std::max(width, fFrames.back().right - kPadding);
	width += 2 * kPadding + 40;	// room for the page arrows
	float height = kPadding * 2
		+ ceilf(candidateHeight.ascent + candidateHeight.descent) + 4;
	if (!fSpelled.IsEmpty())
		height += ceilf(spelledHeight.ascent + spelledHeight.descent) + kRowGap;
	return BSize(ceilf(width), ceilf(height));
}


void
CandidateView::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	rgb_color background = ui_color(B_MENU_BACKGROUND_COLOR);
	rgb_color text = ui_color(B_MENU_ITEM_TEXT_COLOR);
	rgb_color selected = ui_color(B_MENU_SELECTED_BACKGROUND_COLOR);
	rgb_color selectedText = ui_color(B_MENU_SELECTED_ITEM_TEXT_COLOR);
	rgb_color dim = Mix(text, background, 0.45f);

	SetHighColor(background);
	FillRect(bounds);
	SetHighColor(Mix(text, background, 0.7f));
	StrokeRect(bounds);

	SetDrawingMode(B_OP_OVER);
	font_height spelledHeight;
	fSpelledFont.GetHeight(&spelledHeight);
	SetFont(&fSpelledFont);
	SetHighColor(dim);
	DrawString(fSpelled.String(),
		BPoint(kPadding + 4, kPadding + ceilf(spelledHeight.ascent)));

	font_height candidateHeight;
	fCandidateFont.GetHeight(&candidateHeight);
	SetFont(&fCandidateFont);
	for (size_t i = 0; i < fCandidates.size(); i++) {
		BRect frame = fFrames[i];
		bool highlighted = int32(i) == fHighlight;
		if (highlighted) {
			SetHighColor(selected);
			FillRoundRect(frame, 4, 4);
		} else if (int32(i) == fHover) {
			SetHighColor(Mix(selected, background, 0.75f));
			FillRoundRect(frame, 4, 4);
		}
		float baseline = frame.top + 2 + ceilf(candidateHeight.ascent);
		BString number;
		number << (i + 1) << ". ";
		SetHighColor(highlighted ? selectedText : dim);
		DrawString(number.String(), BPoint(frame.left + 4, baseline));
		SetHighColor(highlighted ? selectedText : text);
		DrawString(fCandidates[i].String());
		if (int32(i) == fCloud) {
			SetHighColor(highlighted ? selectedText : dim);
			DrawString(kCloudMark);
		}
	}

	// Page arrows: dim when there's nowhere to go.
	if (!fFrames.empty()) {
		float y = (fFrames[0].top + fFrames[0].bottom) / 2;
		float x = bounds.right - kPadding - 20;
		fPreviousArrow = BRect(x - 4, y - 9, x + 9, y + 9);
		fNextArrow = BRect(x + 9, y - 9, x + 22, y + 9);
		SetHighColor(fPrevious ? text : Mix(text, background, 0.8f));
		FillTriangle(BPoint(x, y), BPoint(x + 6, y - 5), BPoint(x + 6, y + 5));
		x += 12;
		SetHighColor(fNext ? text : Mix(text, background, 0.8f));
		FillTriangle(BPoint(x + 6, y), BPoint(x, y - 5), BPoint(x, y + 5));
	}
}


void
CandidateView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	int32 hover = -1;
	if (transit != B_EXITED_VIEW && transit != B_OUTSIDE_VIEW) {
		for (size_t i = 0; i < fFrames.size(); i++) {
			if (fFrames[i].Contains(where))
				hover = int32(i);
		}
	}
	if (hover != fHover) {
		fHover = hover;
		Invalidate();
	}
}


void
CandidateView::MessageReceived(BMessage* message)
{
	if (message->what == B_MOUSE_WHEEL_CHANGED) {
		float delta = message->GetFloat("be:wheel_delta_y", 0);
		if (delta == 0)
			delta = message->GetFloat("be:wheel_delta_x", 0);
		if (delta != 0) {
			BMessage turn(kMsgTurnPage);
			turn.AddInt32("delta", delta > 0 ? 1 : -1);
			fTarget.SendMessage(&turn);
		}
		return;
	}
	BView::MessageReceived(message);
}


void
CandidateView::MouseDown(BPoint where)
{
	if (fPrevious && fPreviousArrow.Contains(where)) {
		BMessage turn(kMsgTurnPage);
		turn.AddInt32("delta", -1);
		fTarget.SendMessage(&turn);
		return;
	}
	if (fNext && fNextArrow.Contains(where)) {
		BMessage turn(kMsgTurnPage);
		turn.AddInt32("delta", 1);
		fTarget.SendMessage(&turn);
		return;
	}
	for (size_t i = 0; i < fFrames.size(); i++) {
		if (fFrames[i].Contains(where)) {
			BMessage pick(kMsgPickCandidate);
			pick.AddInt32("index", int32(i));
			fTarget.SendMessage(&pick);
			return;
		}
	}
}


// #pragma mark - CandidateWindow


CandidateWindow::CandidateWindow(BMessenger target)
	:
	BWindow(BRect(100, 100, 300, 150), "海酷拼音", B_NO_BORDER_WINDOW_LOOK,
		B_FLOATING_ALL_WINDOW_FEEL,
		B_NOT_MOVABLE | B_NOT_CLOSABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE
			| B_NOT_RESIZABLE | B_AVOID_FOCUS | B_WILL_ACCEPT_FIRST_CLICK,
		B_ALL_WORKSPACES),
	fAnchor(100, 100),
	fLineHeight(16),
	fAnchored(false)
{
	fView = new CandidateView(target);
	fView->ResizeTo(Bounds().Width(), Bounds().Height());
	AddChild(fView);
}


void
CandidateWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgShowCandidates:
		{
			fView->SetContents(*message);
			BSize size = fView->ContentSize();
			ResizeTo(size.width, size.height);
			if (!fAnchored) {
				// The view didn't say where its caret is (not input method
				// aware): sit by the mouse.
				BPoint mouse;
				if (message->FindPoint("mouse", &mouse) == B_OK) {
					fAnchor = mouse + BPoint(0, 20);
					fLineHeight = 0;
				}
			}
			_Place();
			if (IsHidden())
				Show();
			break;
		}
		case kMsgHideCandidates:
			if (!IsHidden())
				Hide();
			fAnchored = false;
			break;
		case kMsgPlaceCandidates:
			message->FindPoint("where", &fAnchor);
			fLineHeight = message->GetFloat("height", 16);
			fAnchored = true;
			_Place();
			break;
		default:
			BWindow::MessageReceived(message);
	}
}


void
CandidateWindow::_Place()
{
	BRect screen = BScreen(this).Frame();
	BRect frame = Frame();
	BPoint where(fAnchor.x - kPadding, fAnchor.y + fLineHeight + 4);
	if (where.y + frame.Height() > screen.bottom)
		where.y = fAnchor.y - frame.Height() - 4;	// above the text instead
	where.x = std::max(screen.left,
		std::min(where.x, screen.right - frame.Width()));
	where.y = std::max(screen.top, where.y);
	MoveTo(where);
}
