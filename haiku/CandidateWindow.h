// Haiku Pinyin: the floating candidate list, 搜狗-style: the spelled
// pinyin above a row of numbered candidates.
#pragma once

#include <Messenger.h>
#include <String.h>
#include <View.h>
#include <Window.h>

#include <vector>

enum {
	kMsgShowCandidates	= 'hpSh',
		// "spelled" string, "candidate" strings, "highlight" int32,
		// "previous"/"next" bool
	kMsgHideCandidates	= 'hpHd',
	kMsgPlaceCandidates	= 'hpPl',
		// "where" point: the top left of the composition; "height" float
	kMsgPickCandidate	= 'hpPk',
		// "index" int32, sent to the method's looper on a click
	kMsgTurnPage		= 'hpTp',
		// "delta" int32: the wheel or a page arrow, to the method's looper
};


class CandidateView : public BView {
public:
								CandidateView(BMessenger target);

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				MessageReceived(BMessage* message);

			void				SetContents(const BMessage& message);
			BSize				ContentSize();

private:
			void				_Layout();

			BMessenger			fTarget;
			BString				fSpelled;
			std::vector<BString> fCandidates;
			std::vector<BRect>	fFrames;
			int32				fHighlight;
			int32				fHover;
			int32				fCloud;		// index of the cloud candidate
			BRect				fPreviousArrow;
			BRect				fNextArrow;
			bool				fPrevious;
			bool				fNext;
			BFont				fCandidateFont;
			BFont				fSpelledFont;
};


class CandidateWindow : public BWindow {
public:
								CandidateWindow(BMessenger target);

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Place();

			CandidateView*		fView;
			BPoint				fAnchor;
			float				fLineHeight;
			bool				fAnchored;
};
