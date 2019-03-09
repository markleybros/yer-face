#pragma once

#include "Logger.hpp"
#include "Utilities.hpp"

#include "SDL.h"

using namespace std;

namespace YerFace {

#define YERFACE_PREVIEW_DEBUG_DENSITY_MAX 5

enum PreviewPositionInFrame {
	BottomLeft,
	BottomRight,
	TopRight
};

enum PreviewPositionInFrameDirection {
	MoveUp,
	MoveDown,
	MoveLeft,
	MoveRight
};

class Status {
public:
	Status(bool myLowLatency);
	~Status();
	void setIsRunning(bool newisRunning);
	bool getIsRunning(void);
	void setIsPaused(bool newIsPaused);
	bool toggleIsPaused(void);
	bool getIsPaused(void);
	void setPreviewPositionInFrame(PreviewPositionInFrame newPosition);
	PreviewPositionInFrame movePreviewPositionInFrame(PreviewPositionInFrameDirection moveDirection);
	PreviewPositionInFrame getPreviewPositionInFrame(void);
	void setPreviewDebugDensity(int newDensity);
	int incrementPreviewDebugDensity(void);
	int getPreviewDebugDensity(void);

private:
	bool lowLatency;
	bool isRunning;
	bool isPaused;
	int previewDebugDensity;
	PreviewPositionInFrame previewPositionInFrame;

	Logger *logger;
	SDL_mutex *myMutex;
};

}; //namespace YerFace
