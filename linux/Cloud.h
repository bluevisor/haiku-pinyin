// 海酷拼音 Haiku Pinyin for Linux: cloud candidates, for both front ends.
#pragma once

#include <functional>
#include <string>
#include <thread>

#include "../engine/Engine.h"

// One question at a time, asked through curl on a thread so neither the
// framework nor the composition waits for the network.
class Cloud {
public:
	// Gets the query and the answer ("" for none), on the cloud's thread.
	typedef std::function<void(const std::string& query,
		const std::string& answer)> Answer;

	~Cloud();

	// Asks Google Input Tools about the composition when the cloud is on,
	// nothing is in flight and it's a new question. answer must hand the
	// reply to the main loop, which calls Answered() and then the engine's
	// SetCloudResult().
	void Ask(const haikupinyin::Engine& engine, Answer answer);
	void Answered() { fAsked.clear(); }

private:
	std::string fAsked;		// in flight
	std::string fAnswered;	// last asked
	std::thread fThread;
};
