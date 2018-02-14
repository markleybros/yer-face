#pragma once

#include "Logger.hpp"
#include "FrameDerivatives.hpp"
#include "FFmpegDriver.hpp"

#include <list>

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

class SDLAudioDevice {
public:
	SDL_AudioSpec desired, obtained;
	SDL_AudioDeviceID deviceID;
	bool opened;
};

class SDLAudioFrame {
public:
	uint8_t *buf;
	int pos;
	int audioSamples;
	int audioBytes;
	int bufferSize;
	double timestamp;
	bool inUse;
};

class SDLDriver {
public:
	SDLDriver(FrameDerivatives *myFrameDerivatives, FFmpegDriver *myFFmpegDriver, bool myAudioPreview = true);
	~SDLDriver();
	SDLWindowRenderer createPreviewWindow(int width, int height);
	SDLWindowRenderer getPreviewWindow(void);
	SDL_Texture *getPreviewTexture(void);
	void doRenderPreviewFrame(void);
	void doHandleEvents(void);
	void onEyedropperEvent(function<void(bool reset, int x, int y)> callback);
	void onBasisFlagEvent(function<void(void)> callback);
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
	static void SDLAudioCallback(void* userdata, Uint8* stream, int len);
	static void FFmpegDriverAudioFrameCallback(void *userdata, uint8_t *buf, int audioSamples, int audioBytes, int bufferSize, double timestamp);
private:
	SDLAudioFrame *getNextAvailableAudioFrame(int desiredBufferSize);

	FrameDerivatives *frameDerivatives;
	FFmpegDriver *ffmpegDriver;
	bool audioPreview;

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

	SDLAudioDevice audioDevice;

	SDL_mutex *audioFramesMutex;
	list<SDLAudioFrame *> audioFrameQueue;
	list<SDLAudioFrame *> audioFramesAllocated;

	SDL_mutex *onEyedropperCallbacksMutex;
	std::vector<function<void(bool reset, int x, int y)>> onEyedropperCallbacks;

	SDL_mutex *onBasisFlagCallbacksMutex;
	std::vector<function<void(void)>> onBasisFlagCallbacks;
};

}; //namespace YerFace
