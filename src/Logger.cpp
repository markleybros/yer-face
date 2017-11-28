
#include "Logger.hpp"

#include <string>

using namespace std;

namespace YerFace {

Logger::Logger(const char *myName, int myCategory) {
	name = (string)myName;
	category = myCategory;
}

void Logger::verbose(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	string extraFormat = name + ": " + (string)fmt;
	SDL_LogMessageV(category, SDL_LOG_PRIORITY_VERBOSE, extraFormat.c_str(), args);
	va_end(args);
}

void Logger::debug(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	string extraFormat = name + ": " + (string)fmt;
	SDL_LogMessageV(category, SDL_LOG_PRIORITY_DEBUG, extraFormat.c_str(), args);
	va_end(args);
}

void Logger::info(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	string extraFormat = name + ": " + (string)fmt;
	SDL_LogMessageV(category, SDL_LOG_PRIORITY_INFO, extraFormat.c_str(), args);
	va_end(args);
}

void Logger::warn(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	string extraFormat = name + ": " + (string)fmt;
	SDL_LogMessageV(category, SDL_LOG_PRIORITY_WARN, extraFormat.c_str(), args);
	va_end(args);
}

void Logger::error(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	string extraFormat = name + ": " + (string)fmt;
	SDL_LogMessageV(category, SDL_LOG_PRIORITY_ERROR, extraFormat.c_str(), args);
	va_end(args);
}

void Logger::critical(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	string extraFormat = name + ": " + (string)fmt;
	SDL_LogMessageV(category, SDL_LOG_PRIORITY_CRITICAL, extraFormat.c_str(), args);
	va_end(args);
}

void Logger::setLoggingFilter(SDL_LogPriority priority, int category) {
	if(category >= 0) {
		SDL_LogSetPriority(category, priority);
	} else {
		SDL_LogSetAllPriority(priority);
	}
}


}; //namespace YerFace
