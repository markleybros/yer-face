#pragma once

#include "Logger.hpp"
#include "FrameDerivatives.hpp"

namespace YerFace {

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
private:
	FrameDerivatives *frameDerivatives;

	Logger *logger;

	SDLWindowRenderer previewWindow;
	SDL_Texture *previewTexture;
};

}; //namespace YerFace
