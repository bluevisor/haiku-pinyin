// 海酷拼音 Haiku Pinyin for Linux: cloud candidates.

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "Shared.h"

static const int kCloudDelay = 120;				// ms, let typing settle


// One question at a time, asked through curl on a thread so neither IBus nor
// the composition waits for the network; the answer comes back on the main
// loop.
struct CloudAnswer {
	std::string query;
	std::string text;
};


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


static gboolean
CloudAnswered(gpointer data)
{
	CloudAnswer* answer = static_cast<CloudAnswer*>(data);
	sShared->cloudAsked.clear();
	if (sShared->loaded
		&& sShared->engine.SetCloudResult(answer->query, answer->text)
		&& sShared->last != nullptr) {
		ShowCandidates(sShared->last);
	}
	delete answer;
	return G_SOURCE_REMOVE;
}


void
AskCloud()
{
	if (!sShared->engine.GetOptions().cloud || !sShared->cloudAsked.empty())
		return;
	std::string query = sShared->engine.CloudQuery();
	if (query.empty() || query == sShared->cloudAnswered)
		return;
	for (char c : query) {
		if (!g_ascii_isalpha(c))
			return;
	}
	sShared->cloudAsked = query;
	sShared->cloudAnswered = query;
	std::thread([query]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(kCloudDelay));
		g_idle_add(CloudAnswered, new CloudAnswer{query, AskGoogle(query)});
	}).detach();
}
