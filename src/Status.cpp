
#include "Status.hpp"
#include "Utilities.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

Status::Status(bool myLowLatency) {
	lowLatency = myLowLatency;
	if((myMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	logger = new Logger("Status");
	logger->debug("Status object constructed and ready to go!");
	setIsRunning(true);
	setIsPaused(false);
}

Status::~Status() {
	logger->debug("Status object destructing...");
	SDL_DestroyMutex(myMutex);
	delete logger;
}

void Status::setIsRunning(bool newIsRunning) {
	YerFace_MutexLock(myMutex);
	isRunning = newIsRunning;
	logger->info("Running is set to %s...", isRunning ? "TRUE" : "FALSE");
	YerFace_MutexUnlock(myMutex);
}

bool Status::getIsRunning(void) {
	YerFace_MutexLock(myMutex);
	bool status = isRunning;
	YerFace_MutexUnlock(myMutex);
	return status;
}

void Status::setIsPaused(bool newIsPaused) {
	YerFace_MutexLock(myMutex);
	if(newIsPaused && lowLatency) {
		logger->warn("Processing cannot be set to PAUSED in lowLatency mode.");
		YerFace_MutexUnlock(myMutex);
	}
	isPaused = newIsPaused;
	logger->info("Processing is set to %s...", isPaused ? "PAUSED" : "RESUMED");
	YerFace_MutexUnlock(myMutex);
}

bool Status::toggleIsPaused(void) {
	YerFace_MutexLock(myMutex);
	setIsPaused(!isPaused);
	bool status = isPaused;
	YerFace_MutexUnlock(myMutex);
	return status;
}

bool Status::getIsPaused(void) {
	YerFace_MutexLock(myMutex);
	bool status = isPaused;
	YerFace_MutexUnlock(myMutex);
	return status;
}

} //namespace YerFace
