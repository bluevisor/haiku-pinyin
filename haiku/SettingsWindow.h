// Haiku Pinyin: the settings window, opened from the Deskbar menu. It lives
// in the input_server beside the method and hands each change to the
// method's looper, which applies and saves it at once.
#pragma once

#include <Messenger.h>
#include <Window.h>

#include "../engine/Engine.h"

class BCheckBox;
class BMenuField;
class BStringView;

enum {
	// To the method's looper.
	kMsgShowSettings	= 'hpSW',
	kMsgApplySettings	= 'hpAS',	// the options, as StoreOptions() puts them
	kMsgEditPhrases		= 'hpPh',
	kMsgShowImport		= 'hpSI',
	kMsgShowExport		= 'hpSE',
	kMsgOpenFolder		= 'hpFo',
	// To the window: the options as the method has them now.
	kMsgSettingsChanged	= 'hpSC',
};

// The options in a message: the settings file's fields.
void StoreOptions(BMessage* message, const haikupinyin::Options& options);
haikupinyin::Options LoadOptions(const BMessage& message);


class SettingsWindow : public BWindow {
public:
								SettingsWindow(BMessenger method,
									const haikupinyin::Options& options);

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Show(const haikupinyin::Options& options);
			haikupinyin::Options	_Chosen() const;
			void				_Report();

			BMessenger			fMethod;
			haikupinyin::Options	fOptions;
			BMenuField*			fLayout;
			BMenuField*			fVowels;
			BMenuField*			fScript;
			BMenuField*			fPageSize;
			BCheckBox*			fFullwidth;
			BCheckBox*			fEmoji;
			BCheckBox*			fEnglish;
			BCheckBox*			fCloud;
			BCheckBox*			fFuzzy[3];
};
