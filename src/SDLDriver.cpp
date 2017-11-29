
#include "SDLDriver.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>

#include "SDL.h"

using namespace std;

namespace YerFace {

SDLDriver::SDLDriver(FrameDerivatives *myFrameDerivatives) {
	logger = new Logger("SDLDriver");

	isRunning = true;
	previewWindow.window = NULL;
	previewWindow.renderer = NULL;
	previewTexture = NULL;
	onQuitCallbacks.clear();

	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}

	if(SDL_Init(SDL_INIT_VIDEO) != 0) {
		logger->error("Unable to initialize SDL: %s", SDL_GetError());
	}
	atexit(SDL_Quit);

	logger->debug("SDLDriver object constructed and ready to go!");
}

SDLDriver::~SDLDriver() {
	if(previewWindow.window != NULL) {
		SDL_DestroyWindow(previewWindow.window);
	}
	if(previewWindow.renderer != NULL) {
		SDL_DestroyRenderer(previewWindow.renderer);
	}
	logger->debug("SDLDriver object destructing...");
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
	Size frameSize = frameDerivatives->getCurrentFrameSize();
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
	Mat previewFrame = frameDerivatives->getPreviewFrame();
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
				handleQuitEvent();
				break;
			case SDL_KEYUP:
				switch(event.key.keysym.sym) {
					case SDLK_ESCAPE:
						handleQuitEvent();
						break;
				}
				break;
		}
	}
}

bool SDLDriver::getIsRunning(void) {
	return isRunning;
}

void SDLDriver::onQuitEvent(function<void(void)> callback) {
	onQuitCallbacks.push_back(callback);
}

void SDLDriver::handleQuitEvent(void) {
	logger->info("Received Quit event from SDL or keyboard escape. Re-broadcasting...");
	isRunning = false;
	invokeAll(onQuitCallbacks);
}

void SDLDriver::invokeAll(vector<function<void(void)>> callbacks) {
	for(auto callback : callbacks) {
		callback();
	}
}

}; //namespace YerFace
