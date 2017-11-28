
#include "SDLDriver.hpp"

#include <cstdio>
#include <cstdlib>

#include "SDL.h"

using namespace std;

namespace YerFace {

SDLDriver::SDLDriver() {
	logger = new Logger("SDLDriver");
	if(SDL_Init(SDL_INIT_VIDEO) != 0) {
		logger->error("Unable to initialize SDL: %s", SDL_GetError());
	}
	logger->debug("SDLDriver object constructed and ready to go!");
}

SDLDriver::~SDLDriver() {
	logger->debug("SDLDriver object destructing...\n");
}

}; //namespace YerFace
