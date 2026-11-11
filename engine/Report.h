// Haiku Pinyin: the link to a new bug report, prefilled from
// .github/ISSUE_TEMPLATE/bug_report.yml, for the Linux and Haiku settings
// windows. It carries the version, the system and the settings; never
// anything typed or learned.
#pragma once

#include <string>

#include "Engine.h"

namespace haikupinyin {

// The template's layout option for an engine layout name.
std::string ReportLayout(const std::string& layout);

// The settings, one per line, for the template's "Other settings" field.
std::string ReportSettings(const Options& options);

// platform is one of the template's: "Linux, IBus", "Linux, Fcitx5",
// "Haiku".
std::string ReportUrl(const std::string& platform, const std::string& version,
	const std::string& system, const Options& options);

}	// namespace haikupinyin
