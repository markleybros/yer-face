#pragma once

#include "Logger.hpp"
#include "Status.hpp"
#include "FrameServer.hpp"
#include "FFmpegDriver.hpp"

#include <list>

namespace YerFace {

#define YERFACE_AUDIO_LATE_GRACE 0.1

class SDLWindowRenderer {
public:
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_RendererInfo rendererInfo;
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

class SDLTextures {
public:
	SDL_Texture *videoTexture;
	cv::Size hudSize;
};

class SDLJoystickDevice {
public:
	int index;
	SDL_JoystickID id;
	SDL_Joystick *joystick;
	string name;
	int axesNum;
	int buttonsNum;
	vector<Uint32> buttonsPressedTime;
	int buttonEventMappingBasis;
	int buttonEventMappingPreviewDebugDensity;
	double axisMin;
	double axisMax;
};

class SDLDriver {
public:
	SDLDriver(json config, Status *myStatus, FrameServer *myFrameServer, FFmpegDriver *myFFmpegDriver, bool myHeadless = false, bool myAudioPreview = true);
	~SDLDriver() noexcept(false);
	SDLWindowRenderer createPreviewWindow(int width, int height, string windowTitle);
	SDLWindowRenderer getPreviewWindow(void);
	void initializePreviewTextures(cv::Size textureSize);
	void doRenderPreviewFrame(cv::Mat previewFrame);
	void doHandleEvents(void);
	void onBasisFlagEvent(function<void(void)> callback);
	void onJoystickButtonEvent(function<void(int deviceId, int button, bool pressed, double heldSeconds)> callback);
	void onJoystickAxisEvent(function<void(int deviceId, int axis, double value)> callback);
	static void SDLAudioCallback(void* userdata, Uint8* stream, int len);
	static void FFmpegDriverAudioFrameCallback(void *userdata, uint8_t *buf, int audioSamples, int audioBytes, double timestamp);
	static void handleFrameStatusChange(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps);
	void stopAudioDriverNow(void);
private:
	SDLAudioFrame *getNextAvailableAudioFrame(int desiredBufferSize);

	Status *status;
	FrameServer *frameServer;
	FFmpegDriver *ffmpegDriver;
	bool headless;
	bool audioPreview;
	bool joystickEnabled;
	bool joystickEventsRaw;

	Logger *logger;

	SDLWindowRenderer previewWindow;
	string previewWindowTitle;
	SDLTextures previewTextures;

	SDLAudioDevice audioDevice;

	SDL_mutex *audioFramesMutex;
	list<SDLAudioFrame *> audioFrameQueue;
	list<SDLAudioFrame *> audioFramesAllocated;

	SDL_mutex *callbacksMutex;
	std::vector<function<void(void)>> onBasisFlagCallbacks;
	std::vector<function<void(int deviceId, int button, bool pressed, double heldSeconds)>> onJoystickButtonEventCallbacks;
	std::vector<function<void(int deviceId, int axis, double value)>> onJoystickAxisEventCallbacks;

	SDL_mutex *frameTimestampsNowMutex;
	FrameTimestamps frameTimestampsNow;

	unordered_map<SDL_JoystickID, SDLJoystickDevice> joysticks;
};

}; //namespace YerFace
