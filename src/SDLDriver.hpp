#pragma once

#include "Logger.hpp"

namespace YerFace {

class SDLDriver {
public:
	SDLDriver();
	~SDLDriver();
private:
	Logger *logger;
};

}; //namespace YerFace
