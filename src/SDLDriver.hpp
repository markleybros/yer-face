#pragma once

#include "Logger.hpp"
#include "FrameDerivatives.hpp"

namespace YerFace {

#define YERFACE_PREVIEW_DEBUG_DENSITY_MAX 4

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

class SDLWindowRenderer {
public:
	SDL_Window *window;
	SDL_Renderer *renderer;
};

class SDLDriver {
public:
	SDLDriver(FrameDerivatives *myFrameDerivatives);
	~SDLDriver();
	SDLWindowRenderer createPreviewWindow(int width, int height);
	SDLWindowRenderer getPreviewWindow(void);
	SDL_Texture *getPreviewTexture(void);
	void doRenderPreviewFrame(void);
	void doHandleEvents(void);
	void onColorPickerEvent(function<void(void)> callback);
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
	void invokeAll(vector<function<void(void)>> callbacks);

	FrameDerivatives *frameDerivatives;

	Logger *logger;

	bool isRunning;
	SDL_mutex *isRunningMutex;
	bool isPaused;
	SDL_mutex *isPausedMutex;
	PreviewPositionInFrame previewPositionInFrame;
	SDL_mutex *previewPositionInFrameMutex;
	int previewDebugDensity;
	SDL_mutex *previewDebugDensityMutex;

	SDLWindowRenderer previewWindow;
	SDL_Texture *previewTexture;

	SDL_mutex *onColorPickerCallbacksMutex;
	vector<function<void(void)>> onColorPickerCallbacks;
};

}; //namespace YerFace
