#pragma once

#include "SDL.h"

#include <cstdarg>

namespace YerFace {

class Logger {
public:
	Logger(const char *myName, int myCategory = SDL_LOG_CATEGORY_APPLICATION);
	void verbose(const char* fmt, ...);
	void debug(const char* fmt, ...);
	void info(const char* fmt, ...);
	void warn(const char* fmt, ...);
	void error(const char* fmt, ...);
	void critical(const char* fmt, ...);
	static void setLoggingFilter(SDL_LogPriority priority, int category = -1);
private:
	const char *name;
	int category;
};

}; //namespace YerFace
