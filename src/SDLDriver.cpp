
#include "SDLDriver.hpp"

#include "Utilities.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>

#include "SDL.h"

using namespace std;

namespace YerFace {

SDLDriver::SDLDriver(FrameDerivatives *myFrameDerivatives) {
	logger = new Logger("SDLDriver");

	if((isRunningMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((isPausedMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((previewPositionInFrameMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((previewDebugDensityMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}
	if((onColorPickerCallbacksMutex = SDL_CreateMutex()) == NULL) {
		throw runtime_error("Failed creating mutex!");
	}

	setIsRunning(true);
	setIsPaused(false);
	setPreviewPositionInFrame(BottomRight);
	setPreviewDebugDensity(1);
	previewWindow.window = NULL;
	previewWindow.renderer = NULL;
	previewTexture = NULL;
	onColorPickerCallbacks.clear();

	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}

	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { //FIXME - make audio optional
		logger->error("Unable to initialize SDL: %s", SDL_GetError());
	}
	atexit(SDL_Quit);

	audioDevice.opened = false;
	//FIXME - make audio optional
	SDL_zero(audioDevice.desired);
	audioDevice.desired.freq = 44100;
	audioDevice.desired.format = AUDIO_S16LSB;
	audioDevice.desired.channels = 2;
	audioDevice.desired.samples = 4096;
	audioDevice.desired.userdata = (void *)this;
	audioDevice.desired.callback = SDLDriver::SDLAudioCallback;
	audioDevice.deviceID = SDL_OpenAudioDevice(NULL, 0, &audioDevice.desired, &audioDevice.obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);
	if(audioDevice.deviceID == 0) {
		logger->error("SDL error opening audio device: %s", SDL_GetError());
		throw runtime_error("failed opening audio device!");
	}
	audioDevice.opened = true;
	logger->info("Opened Audio Output << %d hz, %d channels, %d-bit %s %s %s samples >>", (int)audioDevice.obtained.freq, (int)audioDevice.obtained.channels, (int)SDL_AUDIO_BITSIZE(audioDevice.obtained.format), SDL_AUDIO_ISSIGNED(audioDevice.obtained.format) ? "signed" : "unsigned", SDL_AUDIO_ISBIGENDIAN(audioDevice.obtained.format) ? "big-endian" : "little-endian", SDL_AUDIO_ISFLOAT(audioDevice.obtained.format) ? "float" : "int");
	logger->info("silence value is %d", (int)audioDevice.obtained.silence);
	SDL_PauseAudioDevice(audioDevice.deviceID, 0);

	logger->debug("SDLDriver object constructed and ready to go!");
}

SDLDriver::~SDLDriver() {
	logger->debug("SDLDriver object destructing...");
	if(audioDevice.opened) {
		SDL_CloseAudioDevice(audioDevice.deviceID);
	}
	if(previewWindow.window != NULL) {
		SDL_DestroyWindow(previewWindow.window);
	}
	if(previewWindow.renderer != NULL) {
		SDL_DestroyRenderer(previewWindow.renderer);
	}
	SDL_DestroyMutex(isRunningMutex);
	SDL_DestroyMutex(isPausedMutex);
	SDL_DestroyMutex(previewPositionInFrameMutex);
	SDL_DestroyMutex(previewDebugDensityMutex);
	SDL_DestroyMutex(onColorPickerCallbacksMutex);
	delete logger;
}

SDLWindowRenderer SDLDriver::createPreviewWindow(int width, int height) {
	if(previewWindow.window != NULL || previewWindow.renderer != NULL) {
		throw logic_error("SDL Driver asked to create a preview window, but one already exists!");
	}
	logger->info("Creating Preview SDL Window <%dx%d>", width, height);
	previewWindow.window = SDL_CreateWindow("YerFace! Preview", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_SHOWN);
	if(previewWindow.window == NULL) {
		throw runtime_error("SDL Driver tried to create a preview window and failed.");
	}
	logger->verbose("Creating Preview SDL Renderer.");
	previewWindow.renderer = SDL_CreateRenderer(previewWindow.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if(previewWindow.renderer == NULL) {
		throw runtime_error("SDL Driver tried to create a preview renderer and failed.");
	}
	return previewWindow;
}

SDLWindowRenderer SDLDriver::getPreviewWindow(void) {
	return previewWindow;
}

SDL_Texture *SDLDriver::getPreviewTexture(void) {
	if(previewTexture != NULL) {
		return previewTexture;
	}
	if(previewWindow.renderer == NULL) {
		throw logic_error("SDLDriver::getPreviewTexture() was called, but there is no preview window renderer!");
	}
	Size frameSize = frameDerivatives->getWorkingFrameSize();
	logger->verbose("Creating Preview SDL Texture <%dx%d>", frameSize.width, frameSize.height);
	previewTexture = SDL_CreateTexture(previewWindow.renderer, SDL_PIXELFORMAT_BGR24, SDL_TEXTUREACCESS_STREAMING, frameSize.width, frameSize.height);
	if(previewTexture == NULL) {
		throw runtime_error("SDL Driver was not able to create a preview texture!");
	}
	return previewTexture;
}

void SDLDriver::doRenderPreviewFrame(void) {
	if(previewWindow.window == NULL || previewWindow.renderer == NULL) {
		throw logic_error("SDL Driver asked to render preview frame, but no preview window/renderer exists!");
	}

	SDL_Texture *texture = getPreviewTexture();
	Mat previewFrame = frameDerivatives->getCompletedPreviewFrame();
	Size previewFrameSize = previewFrame.size();

	int textureWidth, textureHeight;
	SDL_QueryTexture(texture, NULL, NULL, &textureWidth, &textureHeight);
	if(textureWidth != previewFrameSize.width || textureHeight != previewFrameSize.height) {
		throw runtime_error("Texture and frame dimension mismatch! Somebody has yanked the rug out from under us.");
	}

	unsigned char * textureData = NULL;
	int texturePitch = 0;
	SDL_LockTexture(texture, 0, (void **)&textureData, &texturePitch);
	memcpy(textureData, (void *)previewFrame.data, previewFrameSize.width * previewFrameSize.height * previewFrame.channels());
	SDL_UnlockTexture(texture);

	SDL_RenderClear(previewWindow.renderer);
	SDL_RenderCopy(previewWindow.renderer, texture, NULL, NULL);
	SDL_RenderPresent(previewWindow.renderer);
}

void SDLDriver::doHandleEvents(void) {
	SDL_Event event;
	while(SDL_PollEvent(&event)){
		switch(event.type) {
			case SDL_QUIT:
				setIsRunning(false);
				break;
			case SDL_KEYDOWN:
				switch(event.key.keysym.sym) {
					case SDLK_ESCAPE:
						setIsRunning(false);
						break;
					case SDLK_SPACE:
						toggleIsPaused();
						break;
					case SDLK_PERIOD:
						setIsPaused(true);
						logger->info("Received Color Picker keyboard event. Rebroadcasting...");
						invokeAll(onColorPickerCallbacks);
						break;
					case SDLK_LEFT:
						movePreviewPositionInFrame(MoveLeft);
						break;
					case SDLK_UP:
						movePreviewPositionInFrame(MoveUp);
						break;
					case SDLK_RIGHT:
						movePreviewPositionInFrame(MoveRight);
						break;
					case SDLK_DOWN:
						movePreviewPositionInFrame(MoveDown);
						break;
					case SDLK_d:
						incrementPreviewDebugDensity();
						break;
				}
				break;
		}
	}
}

void SDLDriver::setIsRunning(bool newIsRunning) {
	YerFace_MutexLock(isRunningMutex);
	isRunning = newIsRunning;
	YerFace_MutexUnlock(isRunningMutex);
}

bool SDLDriver::getIsRunning(void) {
	YerFace_MutexLock(isRunningMutex);
	bool status = isRunning;
	YerFace_MutexUnlock(isRunningMutex);
	return status;
}

void SDLDriver::setIsPaused(bool newIsPaused) {
	YerFace_MutexLock(isPausedMutex);
	isPaused = newIsPaused;
	logger->info("Processing status is set to %s...", isPaused ? "PAUSED" : "RESUMED");
	YerFace_MutexUnlock(isPausedMutex);
}

bool SDLDriver::toggleIsPaused(void) {
	YerFace_MutexLock(isPausedMutex);
	setIsPaused(!isPaused);
	bool status = isPaused;
	YerFace_MutexUnlock(isPausedMutex);
	return status;
}

bool SDLDriver::getIsPaused(void) {
	YerFace_MutexLock(isPausedMutex);
	bool status = isPaused;
	YerFace_MutexUnlock(isPausedMutex);
	return status;
}

void SDLDriver::setPreviewPositionInFrame(PreviewPositionInFrame newPosition) {
	YerFace_MutexLock(previewPositionInFrameMutex);
	previewPositionInFrame = newPosition;
	YerFace_MutexUnlock(previewPositionInFrameMutex);
}

PreviewPositionInFrame SDLDriver::movePreviewPositionInFrame(PreviewPositionInFrameDirection moveDirection) {
	YerFace_MutexLock(previewPositionInFrameMutex);
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
	YerFace_MutexUnlock(previewPositionInFrameMutex);
	return status;
}

PreviewPositionInFrame SDLDriver::getPreviewPositionInFrame(void) {
	YerFace_MutexLock(previewPositionInFrameMutex);
	PreviewPositionInFrame status = previewPositionInFrame;
	YerFace_MutexUnlock(previewPositionInFrameMutex);
	return status;
}

void SDLDriver::setPreviewDebugDensity(int newDensity) {
	YerFace_MutexLock(previewDebugDensityMutex);
	if(newDensity < 0) {
		previewDebugDensity = 0;
	} else if(newDensity > YERFACE_PREVIEW_DEBUG_DENSITY_MAX) {
		previewDebugDensity = YERFACE_PREVIEW_DEBUG_DENSITY_MAX;
	} else {
		previewDebugDensity = newDensity;
	}
	YerFace_MutexUnlock(previewDebugDensityMutex);
}

int SDLDriver::incrementPreviewDebugDensity(void) {
	YerFace_MutexLock(previewDebugDensityMutex);
	previewDebugDensity++;
	if(previewDebugDensity > YERFACE_PREVIEW_DEBUG_DENSITY_MAX) {
		previewDebugDensity = 0;
	}
	int status = previewDebugDensity;
	YerFace_MutexUnlock(previewDebugDensityMutex);
	return status;
}

int SDLDriver::getPreviewDebugDensity(void) {
	YerFace_MutexLock(previewDebugDensityMutex);
	int status = previewDebugDensity;
	YerFace_MutexUnlock(previewDebugDensityMutex);
	return status;
}

void SDLDriver::onColorPickerEvent(function<void(void)> callback) {
	YerFace_MutexLock(onColorPickerCallbacksMutex);
	onColorPickerCallbacks.push_back(callback);
	YerFace_MutexUnlock(onColorPickerCallbacksMutex);
}

void SDLDriver::invokeAll(vector<function<void(void)>> callbacks) {
	YerFace_MutexLock(onColorPickerCallbacksMutex);
	for(auto callback : callbacks) {
		callback();
	}
	YerFace_MutexUnlock(onColorPickerCallbacksMutex);
}

void SDLDriver::SDLAudioCallback(void* userdata, Uint8* stream, int len) {
	SDLDriver *self = (SDLDriver *)userdata;
	memset(stream, self->audioDevice.obtained.silence, len);
}

}; //namespace YerFace
