// hpsettings: write 海酷拼音's settings file (development; the Deskbar menu is
// the real way). Restart the input_server to apply.
//   hpsettings layout=pyjj script=0..3 page=7 fullwidth=1 chinese=1 emoji=1

#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int
main(int argc, char** argv)
{
	BPath path;
	find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	path.Append("HaikuPinyin/settings");
	BMessage settings;
	settings.AddString("layout", "full");
	settings.AddInt32("script", 0);
	settings.AddInt32("page size", 7);
	settings.AddBool("fullwidth", true);
	settings.AddBool("chinese", true);
	settings.AddBool("emoji", true);
	for (int i = 1; i < argc; i++) {
		const char* value = strchr(argv[i], '=');
		if (value++ == NULL)
			continue;
		if (strncmp(argv[i], "layout=", 7) == 0)
			settings.ReplaceString("layout", value);
		else if (strncmp(argv[i], "script=", 7) == 0)
			settings.ReplaceInt32("script", atoi(value));
		else if (strncmp(argv[i], "page=", 5) == 0)
			settings.ReplaceInt32("page size", atoi(value));
		else if (strncmp(argv[i], "fullwidth=", 10) == 0)
			settings.ReplaceBool("fullwidth", atoi(value) != 0);
		else if (strncmp(argv[i], "emoji=", 6) == 0)
			settings.ReplaceBool("emoji", atoi(value) != 0);
		else if (strncmp(argv[i], "chinese=", 8) == 0)
			settings.ReplaceBool("chinese", atoi(value) != 0);
	}
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	return settings.Flatten(&file) == B_OK ? 0 : 1;
}
