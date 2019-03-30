#pragma once

#include "SDL.h"

#include <cstdarg>
#include <string>

namespace YerFace {

class Logger {
public:
	Logger(const char *myName, int myCategory = SDL_LOG_CATEGORY_APPLICATION);
	void verbose(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
	void debug(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
	void info(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
	void warn(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
	void error(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
	void critical(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
	static void setLoggingFilter(SDL_LogPriority priority, int category = -1);
private:
	std::string name;
	int category;
};

}; //namespace YerFace
