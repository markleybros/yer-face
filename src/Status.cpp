
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
	logger->debug1("Status object constructed and ready to go!");
	setIsRunning(true);
	setIsPaused(false);
	setPreviewPositionInFrame(BottomRight);
	previewDebugDensity = 0;
}

Status::~Status() noexcept(false) {
	logger->debug1("Status object destructing...");
	SDL_DestroyMutex(myMutex);
	delete logger;
}

void Status::setEmergency(void) {
	YerFace_MutexLock(myMutex);
	if(!emergency) {
		logger->emerg("Initiated Emergency Stop");
	}
	emergency = true;
	setIsRunning(false);
	YerFace_MutexUnlock(myMutex);
}

bool Status::getEmergency(void) {
	YerFace_MutexLock(myMutex);
	bool status = emergency;
	YerFace_MutexUnlock(myMutex);
	return status;
}

void Status::setIsRunning(bool newIsRunning) {
	YerFace_MutexLock(myMutex);
	if(newIsRunning != isRunning) {
		logger->info("Running is set to %s...", newIsRunning ? "TRUE" : "FALSE");
	}
	isRunning = newIsRunning;
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
		logger->warning("Processing cannot be set to PAUSED in lowLatency mode.");
		YerFace_MutexUnlock(myMutex);
		return;
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

void Status::setPreviewPositionInFrame(PreviewPositionInFrame newPosition) {
	YerFace_MutexLock(myMutex);
	previewPositionInFrame = newPosition;
	YerFace_MutexUnlock(myMutex);
}

PreviewPositionInFrame Status::movePreviewPositionInFrame(PreviewPositionInFrameDirection moveDirection) {
	YerFace_MutexLock(myMutex);
	switch(moveDirection) {
		case MoveLeft:
			previewPositionInFrame = BottomLeft;
			break;
		case MoveUp:
			previewPositionInFrame = TopRight;
			break;
		case MoveRight:
			if(previewPositionInFrame == BottomLeft) {
				previewPositionInFrame = BottomRight;
			}
			break;
		case MoveDown:
			if(previewPositionInFrame == TopRight) {
				previewPositionInFrame = BottomRight;
			}
			break;
	}
	PreviewPositionInFrame status = previewPositionInFrame;
	YerFace_MutexUnlock(myMutex);
	return status;
}

PreviewPositionInFrame Status::getPreviewPositionInFrame(void) {
	YerFace_MutexLock(myMutex);
	PreviewPositionInFrame status = previewPositionInFrame;
	YerFace_MutexUnlock(myMutex);
	return status;
}

void Status::setPreviewDebugDensity(int newDensity) {
	YerFace_MutexLock(myMutex);
	if(newDensity < 0) {
		previewDebugDensity = 0;
	} else if(newDensity > YERFACE_PREVIEW_DEBUG_DENSITY_MAX) {
		previewDebugDensity = YERFACE_PREVIEW_DEBUG_DENSITY_MAX;
	} else {
		previewDebugDensity = newDensity;
	}
	logger->info("Preview Debug Density set to %d", previewDebugDensity);
	YerFace_MutexUnlock(myMutex);
}

int Status::incrementPreviewDebugDensity(void) {
	YerFace_MutexLock(myMutex);
	previewDebugDensity++;
	if(previewDebugDensity > YERFACE_PREVIEW_DEBUG_DENSITY_MAX) {
		previewDebugDensity = 0;
	}
	int status = previewDebugDensity;
	logger->info("Preview Debug Density set to %d", previewDebugDensity);
	YerFace_MutexUnlock(myMutex);
	return status;
}

int Status::getPreviewDebugDensity(void) {
	YerFace_MutexLock(myMutex);
	int status = previewDebugDensity;
	YerFace_MutexUnlock(myMutex);
	return status;
}

} //namespace YerFace
