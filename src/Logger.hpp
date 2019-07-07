#pragma once

#include "Utilities.hpp"

#include "SDL.h"

#include <cstdarg>
#include <string>

namespace YerFace {

#ifdef WIN32
typedef unsigned short LogConsoleCode;
#else
typedef std::string LogConsoleCode;
#endif

// Log severity is loosely derived from RFC 5424.
// The primary extension is that severity levels 7 and higher are various levels of debug verbosity.
// Lower severity levels represent increasing levels of criticality.
enum LogMessageSeverity: unsigned int {
	LOG_SEVERITY_EMERG = 0,		//system is unusable
	LOG_SEVERITY_ALERT = 1,		//action must be taken immediately
	LOG_SEVERITY_CRIT = 2,		//critical conditions
	LOG_SEVERITY_ERR = 3,		//error conditions
	LOG_SEVERITY_WARNING = 4,	//warning conditions
	LOG_SEVERITY_NOTICE = 5,	//normal but significant conditions
	LOG_SEVERITY_INFO = 6,		//informational messages
	LOG_SEVERITY_DEBUG1 = 7,	//first-level (lowest density) debug messages
	LOG_SEVERITY_DEBUG2 = 8,	//second-level (medium density) debug messages
	LOG_SEVERITY_DEBUG3 = 9,	//third-level (high level) debug messages
	LOG_SEVERITY_DEBUG4 = 10	//fourth-level (trace) debug messages
};
#define LOG_SEVERITY_MIN (LogMessageSeverity)0
#define LOG_SEVERITY_MAX (LogMessageSeverity)10
#define LOG_SEVERITY_FILTERDEFAULT LOG_SEVERITY_INFO

enum LogColorModes: unsigned int {
	LOG_COLORS_OFF,
	LOG_COLORS_ON,
	LOG_COLORS_AUTO
};

enum LogColorEligibility: unsigned int {
	LOG_COLORS_CONSOLE_ELIGIBLE,
	LOG_COLORS_CONSOLE_INELIGIBLE,
	LOG_COLORS_CONSOLE_ELIGIBILITY_UNKNOWN
};

class Logger {
public:
	Logger(const char *myName);
	#if defined(__GNUC__)
	void debug4(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
	void debug3(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
	void debug2(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
	void debug1(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
	void info(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
	void notice(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
	void warning(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
	void err(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
	void crit(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
	void alert(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
	void emerg(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
	void log(LogMessageSeverity severity, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
	static void slog(std::string moduleName, LogMessageSeverity severity, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
	#else
	void debug4(const char *fmt, ...);
	void debug3(const char *fmt, ...);
	void debug2(const char *fmt, ...);
	void debug1(const char *fmt, ...);
	void info(const char *fmt, ...);
	void notice(const char *fmt, ...);
	void warning(const char *fmt, ...);
	void err(const char *fmt, ...);
	void crit(const char *fmt, ...);
	void alert(const char *fmt, ...);
	void emerg(const char *fmt, ...);
	void log(LogMessageSeverity severity, const char *fmt, ...);
	static void slog(std::string moduleName, LogMessageSeverity severity, const char *fmt, ...);
	#endif
	static void svlog(std::string moduleName, LogMessageSeverity severity, const char *fmt, va_list args);
	static void setLoggingTarget(std::string filePath);
	static void setLoggingTarget(FILE *file);
	static void setLoggingColorMode(LogColorModes mode);
	static void setLoggingFilter(LogMessageSeverity severity);
	static std::string getSeverityString(LogMessageSeverity severity);
private:
	static LogConsoleCode getSeverityStringConsoleCode(LogMessageSeverity severity);

	std::string name;
	static LogMessageSeverity severityFilter;
	static bool outFileOpened;
	static FILE *outFile;
	static LogColorModes colorMode;
	static LogColorEligibility colorsEligible;
	static SDL_mutex *staticMutex;
};

#define YerFace_SLog(moduleName, severity, fmt, ...) \
	Logger::slog(moduleName, severity, "%s:%d: " fmt, YERFACE_FILE, __LINE__, __VA_ARGS__)

}; //namespace YerFace
