#pragma once

#include "Logger.hpp"
#include "Status.hpp"
#include "FrameServer.hpp"
#include "FFmpegDriver.hpp"

#include <list>

namespace YerFace {

#define YERFACE_PREVIEW_DEBUG_DENSITY_MAX 5
#define YERFACE_AUDIO_LATE_GRACE 0.1

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
	SDLDriver(json config, Status *myStatus, FrameServer *myFrameServer, FFmpegDriver *myFFmpegDriver, bool myHeadless = false, bool myAudioPreview = true);
	~SDLDriver();
	SDLWindowRenderer createPreviewWindow(int width, int height);
	SDLWindowRenderer getPreviewWindow(void);
	SDL_Texture *getPreviewTexture(Size textureSize);
	void doRenderPreviewFrame(Mat previewFrame);
	void doHandleEvents(void);
	void onBasisFlagEvent(function<void(void)> callback);
	void setPreviewPositionInFrame(PreviewPositionInFrame newPosition);
	PreviewPositionInFrame movePreviewPositionInFrame(PreviewPositionInFrameDirection moveDirection);
	PreviewPositionInFrame getPreviewPositionInFrame(void);
	void setPreviewDebugDensity(int newDensity);
	int incrementPreviewDebugDensity(void);
	int getPreviewDebugDensity(void);
	void createPreviewHUDRectangle(Size frameSize, Rect2d *previewRect, Point2d *previewCenter);
	static void SDLAudioCallback(void* userdata, Uint8* stream, int len);
	static void FFmpegDriverAudioFrameCallback(void *userdata, uint8_t *buf, int audioSamples, int audioBytes, double timestamp);
	void stopAudioDriverNow(void);
private:
	SDLAudioFrame *getNextAvailableAudioFrame(int desiredBufferSize);

	Status *status;
	FrameServer *frameServer;
	FFmpegDriver *ffmpegDriver;
	bool headless;
	bool audioPreview;

	Logger *logger;

	PreviewPositionInFrame previewPositionInFrame;
	SDL_mutex *previewPositionInFrameMutex;
	int previewDebugDensity;
	SDL_mutex *previewDebugDensityMutex;
	double previewRatio, previewWidthPercentage, previewCenterHeightPercentage;

	SDLWindowRenderer previewWindow;
	SDL_Texture *previewTexture;

	SDLAudioDevice audioDevice;

	SDL_mutex *audioFramesMutex;
	list<SDLAudioFrame *> audioFrameQueue;
	list<SDLAudioFrame *> audioFramesAllocated;

	SDL_mutex *onBasisFlagCallbacksMutex;
	std::vector<function<void(void)>> onBasisFlagCallbacks;
};

}; //namespace YerFace
