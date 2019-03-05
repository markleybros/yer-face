#pragma once

#include "Logger.hpp"
#include "Utilities.hpp"

#include "SDL.h"

using namespace std;

namespace YerFace {

class Status {
public:
	Status(bool myLowLatency);
	~Status();
	void setIsRunning(bool newisRunning);
	bool getIsRunning(void);
	void setIsPaused(bool newIsPaused);
	bool toggleIsPaused(void);
	bool getIsPaused(void);

private:
	bool lowLatency;
	bool isRunning;
	bool isPaused;

	Logger *logger;
	SDL_mutex *myMutex;
};

}; //namespace YerFace
