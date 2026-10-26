// imtest: a text view that mirrors its contents to /tmp/imtest.txt, for
// checking what an input method typed.   imtest [--unaware]

#include <Application.h>
#include <TextView.h>
#include <Window.h>

#include <stdio.h>
#include <string.h>


class MirrorView : public BTextView {
public:
	MirrorView(BRect frame, bool aware)
		: BTextView(frame, "text", frame.OffsetToCopy(0, 0).InsetByCopy(4, 4),
			B_FOLLOW_ALL, B_WILL_DRAW | B_NAVIGABLE)
	{
		if (!aware)
			SetFlags(Flags() & ~B_INPUT_METHOD_AWARE);
		BFont font(be_plain_font);
		font.SetSize(20);
		SetFontAndColor(&font);
	}

protected:
	virtual void InsertText(const char* text, int32 length, int32 offset,
		const text_run_array* runs)
	{
		BTextView::InsertText(text, length, offset, runs);
		_Mirror();
	}

	virtual void DeleteText(int32 from, int32 to)
	{
		BTextView::DeleteText(from, to);
		_Mirror();
	}

private:
	void _Mirror()
	{
		FILE* file = fopen("/tmp/imtest.txt", "w");
		if (file != NULL) {
			fputs(Text(), file);
			fclose(file);
		}
	}
};


int
main(int argc, char** argv)
{
	bool aware = !(argc > 1 && strcmp(argv[1], "--unaware") == 0);
	BApplication app("application/x-vnd.haiku-pinyin-imtest");
	BWindow* window = new BWindow(BRect(200, 200, 900, 400),
		aware ? "imtest" : "imtest (unaware)", B_TITLED_WINDOW,
		B_QUIT_ON_WINDOW_CLOSE);
	MirrorView* view = new MirrorView(window->Bounds(), aware);
	window->AddChild(view);
	view->MakeFocus();
	window->Show();
	app.Run();
	return 0;
}
