// hpkeys: type into the input_server through TestKeysDevice.
//   hpkeys "nihao" @space @enter @bs @esc @left @right @up @down @pgdn
//          @shift (tap) @ime (Cmd+Space on a Mac keyboard: Haiku's Option,
//          which Kigo's filter turns into "next input method") @sleep=300
// Plain text is typed one character at a time; uppercase and symbols carry
// Shift like a real keyboard.

#include <Message.h>
#include <OS.h>
#include <InterfaceDefs.h>

#include <stdio.h>
#include <string.h>
#include <string>


static port_id sPort;


static void
Send(BMessage& message)
{
	message.AddInt64("when", system_time());
	ssize_t size = message.FlattenedSize();
	char buffer[size];
	message.Flatten(buffer, size);
	write_port(sPort, 0, buffer, size);
	snooze(15000);
}


static void
Modifiers(uint32 now, uint32 old)
{
	BMessage message(B_MODIFIERS_CHANGED);
	message.AddInt32("modifiers", now);
	message.AddInt32("be:old_modifiers", old);
	Send(message);
}


static void
Key(const char* bytes, int32 key, uint32 modifiers)
{
	BMessage down(B_KEY_DOWN);
	down.AddInt32("key", key);
	down.AddInt32("modifiers", modifiers);
	down.AddInt32("raw_char", (uint8)bytes[0]);
	down.AddInt8("byte", (int8)bytes[0]);
	down.AddString("bytes", bytes);
	Send(down);
	BMessage up(B_KEY_UP);
	up.AddInt32("key", key);
	up.AddInt32("modifiers", modifiers);
	up.AddInt32("raw_char", (uint8)bytes[0]);
	up.AddInt8("byte", (int8)bytes[0]);
	up.AddString("bytes", bytes);
	Send(up);
}


int
main(int argc, char** argv)
{
	sPort = find_port("haiku pinyin test keys");
	if (sPort < 0) {
		fprintf(stderr, "TestKeysDevice isn't loaded\n");
		return 1;
	}
	const uint32 kShift = B_SHIFT_KEY | B_LEFT_SHIFT_KEY;
	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if (arg.rfind("@sleep=", 0) == 0) {
			snooze(atoi(arg.c_str() + 7) * 1000);
			continue;
		}
		struct { const char* name; char byte; } kNamed[] = {
			{"@space", ' '}, {"@enter", '\n'}, {"@bs", B_BACKSPACE},
			{"@esc", B_ESCAPE}, {"@left", B_LEFT_ARROW},
			{"@right", B_RIGHT_ARROW}, {"@up", B_UP_ARROW},
			{"@down", B_DOWN_ARROW}, {"@pgdn", B_PAGE_DOWN},
			{"@pgup", B_PAGE_UP}, {"@tab", B_TAB},
		};
		bool named = false;
		for (auto& key : kNamed) {
			if (arg == key.name) {
				char bytes[2] = {key.byte, 0};
				Key(bytes, 0x100 + key.byte, 0);
				named = true;
			}
		}
		if (named)
			continue;
		if (arg == "@shift") {
			Modifiers(kShift, 0);
			Modifiers(0, kShift);
			continue;
		}
		if (arg == "@ime") {
			uint32 command = B_OPTION_KEY | B_LEFT_OPTION_KEY;
			Modifiers(command, 0);
			Key(" ", 0x5e, command);
			Modifiers(0, command);
			continue;
		}
		for (char c : arg) {
			char bytes[2] = {c, 0};
			bool shifted = (c >= 'A' && c <= 'Z')
				|| strchr("~!@#$%^&*()_+{}|:\"<>?", c) != NULL;
			if (shifted)
				Modifiers(kShift, 0);
			Key(bytes, (uint8)c, shifted ? kShift : 0);
			if (shifted)
				Modifiers(0, kShift);
		}
	}
	return 0;
}
