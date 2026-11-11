// 海酷拼音 Haiku Pinyin for Linux: cloud candidates.

#include "Cloud.h"

#include <chrono>
#include <cstdio>

static const int kCloudDelay = 120;				// ms, let typing settle


static std::string
AskGoogle(const std::string& query)
{
	// The query is full pinyin: letters only, safe in a URL and a shell.
	std::string command = "curl -s -m 2 'https://inputtools.google.com/"
		"request?text=" + query + "&itc=zh-t-i0-pinyin&num=1&cp=0&cs=1"
		"&ie=utf-8&oe=utf-8&app=haikupinyin' 2>/dev/null";
	FILE* pipe = popen(command.c_str(), "r");
	if (pipe == nullptr)
		return std::string();
	std::string reply;
	char buffer[4096];
	size_t read;
	while ((read = fread(buffer, 1, sizeof(buffer), pipe)) > 0)
		reply.append(buffer, read);
	pclose(pipe);

	// ["SUCCESS",[["<query>",["<answer>", ...
	std::string marker = "[[\"" + query + "\",[\"";
	size_t start = reply.find(marker);
	if (reply.compare(0, 11, "[\"SUCCESS\",") != 0
		|| start == std::string::npos) {
		return std::string();
	}
	start += marker.size();
	size_t end = reply.find('"', start);
	if (end == std::string::npos)
		return std::string();
	std::string answer = reply.substr(start, end - start);
	// Chinese comes back as UTF-8; anything escaped isn't worth offering.
	if (answer.find('\\') != std::string::npos)
		return std::string();
	return answer;
}


Cloud::~Cloud()
{
	if (fThread.joinable())
		fThread.join();
}


void
Cloud::Ask(const haikupinyin::Engine& engine, Answer answer)
{
	if (!engine.GetOptions().cloud || !fAsked.empty())
		return;
	std::string query = engine.CloudQuery();
	if (query.empty() || query == fAnswered)
		return;
	for (char c : query) {
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
			return;
	}
	fAsked = query;
	fAnswered = query;
	// The last one has handed over its answer, so it's done or nearly.
	if (fThread.joinable())
		fThread.join();
	fThread = std::thread([query, answer]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(kCloudDelay));
		answer(query, AskGoogle(query));
	});
}
