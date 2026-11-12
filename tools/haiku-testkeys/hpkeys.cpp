// hpkeys: type into the input_server through TestKeysDevice.
//   hpkeys "nihao" @space @enter @bs @esc @left @right @up @down @pgdn
//          @shift (tap) @ime (Cmd+Space on a Mac keyboard: Haiku's Option,
//          which Kigo's filter turns into "next input method") @sleep=300
//          @ctrl+shift+f (modifiers and a key, from the keymap as the
//          keyboard sends them: bytes 0x06 and raw_char 'f' here)
// Plain text is typed one character at a time; uppercase and symbols carry
// Shift like a real keyboard.

#include <Message.h>
#include <OS.h>
#include <InterfaceDefs.h>

#include <stdio.h>
#include <stdlib.h>
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


// The characters a key gives in one of the keymap's tables, as
// KeyboardInputDevice looks them up.
static std::string
Chars(const char* chars, const int32* table, int32 key)
{
	const char* entry = chars + table[key];
	return std::string(entry + 1, (uint8)entry[0]);
}


// "@ctrl+shift+f": the modifiers go down, the key, then they come up;
// bytes from the Control (or Shift) table and raw_char from the plain one.
static bool
Combination(const std::string& arg)
{
	uint32 modifiers = 0;
	size_t start = 1, plus;
	while ((plus = arg.find('+', start)) != std::string::npos) {
		std::string name = arg.substr(start, plus - start);
		if (name == "ctrl")
			modifiers |= B_CONTROL_KEY | B_LEFT_CONTROL_KEY;
		else if (name == "shift")
			modifiers |= B_SHIFT_KEY | B_LEFT_SHIFT_KEY;
		else if (name == "cmd")
			modifiers |= B_COMMAND_KEY | B_LEFT_COMMAND_KEY;
		else if (name == "opt")
			modifiers |= B_OPTION_KEY | B_LEFT_OPTION_KEY;
		else
			return false;
		start = plus + 1;
	}
	std::string last = arg.substr(start);
	if (modifiers == 0 || last.size() != 1)
		return false;

	key_map* map;
	char* chars;
	get_key_map(&map, &chars);
	int32 key = -1;
	for (int32 i = 0; i < 128 && key < 0; i++) {
		if (Chars(chars, map->normal_map, i) == last)
			key = i;
	}
	if (key < 0) {
		free(map);
		free(chars);
		return false;
	}
	const int32* table = map->normal_map;
	if (modifiers & B_CONTROL_KEY)
		table = map->control_map;
	else if (modifiers & B_SHIFT_KEY)
		table = map->shift_map;
	std::string bytes = Chars(chars, table, key);
	std::string raw = Chars(chars, map->normal_map, key);
	free(map);
	free(chars);

	Modifiers(modifiers, 0);
	for (uint32 what : {B_KEY_DOWN, B_KEY_UP}) {
		BMessage message(what);
		message.AddInt32("key", key);
		message.AddInt32("modifiers", modifiers);
		message.AddInt32("raw_char", (uint8)raw[0] & 0x7f);
		for (char c : bytes)
			message.AddInt8("byte", (int8)c);
		message.AddString("bytes", bytes.c_str());
		Send(message);
	}
	Modifiers(0, modifiers);
	return true;
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
		if (arg.find('+') != std::string::npos && arg[0] == '@') {
			if (!Combination(arg))
				fprintf(stderr, "hpkeys: can't type %s\n", arg.c_str());
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
