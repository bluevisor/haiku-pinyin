// Haiku Pinyin: the input_server method add-on.
#pragma once

#include <InputServerMethod.h>
#include <Locker.h>
#include <Looper.h>
#include <Point.h>

#include <set>
#include <string>

#include "../engine/Engine.h"

class CandidateWindow;
class PinyinMethod;


// Receives the application's replies (caret location, "stop"), clicks in
// the candidate window and Deskbar menu picks, off the input_server's event
// thread.
class MethodLooper : public BLooper {
public:
								MethodLooper(PinyinMethod* method);
	virtual	void				MessageReceived(BMessage* message);

private:
			PinyinMethod*		fMethod;
};


class PinyinMethod : public BInputServerMethod {
public:
								PinyinMethod();
	virtual						~PinyinMethod();

	virtual	status_t			InitCheck();
	virtual	filter_result		Filter(BMessage* message, BList* outList);
	virtual	status_t			MethodActivated(bool active);

			void				LooperMessage(BMessage* message);

private:
			bool				_EnsureLoaded();
			bool				_HandleKey(BMessage* message);
			bool				_HandleModifiers(BMessage* message);
			void				_Sync(BList* events);
			void				_Cancel(BList* events);
			void				_Deliver(BList& events);
			void				_ShowCandidates();
			void				_AskCloud();
			void				_UpdateMenu();
			void				_UpdateIcon();
			void				_RepaintDeskbar();
			void				_LoadSettings();
			void				_SaveSettings();

			BLocker				fLock;
			haikupinyin::Engine		fEngine;
			haikupinyin::Options	fSavedOptions;	// applied once data loads
			bool				fSavedChinese;
			BMenu*				fMenu;
			bool				fLoaded;
			bool				fLoadFailed;
			bool				fActive;

			MethodLooper*		fLooper;
			CandidateWindow*	fWindow;

			bool				fSessionOpen;
			std::string			fShownPreedit;
			size_t				fShownPicked;
			std::set<int32>		fSwallowedKeys;
			BPoint				fMouse;
			std::string			fCloudAsked;	// in flight
			std::string			fCloudAnswered;	// last asked
			bool				fShiftAlone;
			bigtime_t			fShiftDown;
};
