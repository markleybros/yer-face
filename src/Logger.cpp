
#include "Logger.hpp"
#include "Utilities.hpp"

#include <string>
#include <ctime>
#include <sys/time.h> //FIXME - windows support?

#if WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

using namespace std;

namespace YerFace {

// FIXME - windows console color support?
#define CONSOLE_COLOR_RESETALL				"\x1B[0m"
#define CONSOLE_COLOR_FOREGROUND_RED		"\x1B[91m"
#define CONSOLE_COLOR_FOREGROUND_GREEN		"\x1B[92m"
#define CONSOLE_COLOR_FOREGROUND_YELLOW		"\x1B[93m"
#define CONSOLE_COLOR_FOREGROUND_BLUE		"\x1B[94m"
#define CONSOLE_COLOR_FOREGROUND_MAGENTA	"\x1B[95m"
#define CONSOLE_COLOR_FOREGROUND_CYAN		"\x1B[96m"
#define CONSOLE_COLOR_FOREGROUND_LIGHTGRAY	"\x1B[37m"
#define CONSOLE_COLOR_FOREGROUND_DARKGRAY	"\x1B[90m"
#define CONSOLE_FONT_BOLD_ON				"\x1B[1m"
#define CONSOLE_FONT_DIM_ON					"\x1B[2m"
#define CONSOLE_FONT_UNDERLINE_ON			"\x1B[4m"
#define CONSOLE_FONT_REVERSE_ON				"\x1B[7m"
#define CONSOLE_FONT_BOLD_OFF				"\x1B[21m"
#define CONSOLE_FONT_DIM_OFF				"\x1B[22m"
#define CONSOLE_FONT_UNDERLINE_OFF			"\x1B[24m"
#define CONSOLE_FONT_REVERSE_OFF			"\x1B[27m"

LogMessageSeverity Logger::severityFilter = LOG_SEVERITY_FILTERDEFAULT;
bool Logger::outFileOpened = false;
FILE *Logger::outFile = stderr;
LogColorModes Logger::colorMode = LOG_COLORS_AUTO;
LogColorEligibility Logger::colorsEligible = LOG_COLORS_CONSOLE_ELIGIBILITY_UNKNOWN;
SDL_mutex *Logger::staticMutex = SDL_CreateMutex();

Logger::Logger(const char *myName) {
	name = (string)myName;

	if(name.find('%') != string::npos) {
		throw invalid_argument("Logger name must not contain a percent sign.");
	}
}

void Logger::debug4(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vlog(LOG_SEVERITY_DEBUG4, fmt, args);
	va_end(args);
}

void Logger::debug3(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vlog(LOG_SEVERITY_DEBUG3, fmt, args);
	va_end(args);
}

void Logger::debug2(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vlog(LOG_SEVERITY_DEBUG2, fmt, args);
	va_end(args);
}

void Logger::debug1(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vlog(LOG_SEVERITY_DEBUG1, fmt, args);
	va_end(args);
}

void Logger::info(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vlog(LOG_SEVERITY_INFO, fmt, args);
	va_end(args);
}

void Logger::notice(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vlog(LOG_SEVERITY_NOTICE, fmt, args);
	va_end(args);
}

void Logger::warning(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vlog(LOG_SEVERITY_WARNING, fmt, args);
	va_end(args);
}

void Logger::err(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vlog(LOG_SEVERITY_ERR, fmt, args);
	va_end(args);
}

void Logger::crit(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vlog(LOG_SEVERITY_CRIT, fmt, args);
	va_end(args);
}

void Logger::alert(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vlog(LOG_SEVERITY_ALERT, fmt, args);
	va_end(args);
}

void Logger::emerg(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vlog(LOG_SEVERITY_EMERG, fmt, args);
	va_end(args);
}

void Logger::log(LogMessageSeverity severity, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vlog(severity, fmt, args);
	va_end(args);
}

void Logger::vlog(LogMessageSeverity severity, const char *fmt, va_list args) {
	YerFace_MutexLock(staticMutex);
	LogMessageSeverity mySeverityFilter = severityFilter;
	FILE *myOutFile = outFile;
	LogColorModes myColorMode = colorMode;
	if(myColorMode == LOG_COLORS_AUTO) {
		if(colorsEligible == LOG_COLORS_CONSOLE_ELIGIBILITY_UNKNOWN) {
			if(isatty(fileno(myOutFile))) {
				colorsEligible = LOG_COLORS_CONSOLE_ELIGIBLE;
			} else {
				colorsEligible = LOG_COLORS_CONSOLE_INELIGIBLE;
			}
		}
		if(colorsEligible == LOG_COLORS_CONSOLE_ELIGIBLE) {
			myColorMode = LOG_COLORS_ON;
		} else {
			myColorMode = LOG_COLORS_OFF;
		}
	}
	YerFace_MutexUnlock(staticMutex);

	//Drop messages according to the logging filter.
	if(severity > mySeverityFilter) {
		return;
	}

	//Trim any leading or trailing whitespace from fmt.
	string originalFormat = Utilities::stringTrim((string)fmt);

	//Resolve a time string. (FIXME there is effectively zero chance this will work correctly on Windows.)
	struct timeval myTimeVal;
	gettimeofday(&myTimeVal, NULL);
	struct tm myTm;
	localtime_r(&myTimeVal.tv_sec, &myTm); //If we want UTC, use gmtime_r()
	char timeStringIntermediate[64];
	strftime(timeStringIntermediate, sizeof(timeStringIntermediate), "%F %H:%M:%S", &myTm);
	char timeStringC[128];
	int milli = myTimeVal.tv_usec / 1000;
	snprintf(timeStringC, sizeof(timeStringC), "%s.%03d", timeStringIntermediate, milli); //If we're using UTC, add a "Z" to the end of the string
	std::string timeString = (string)timeStringC;

	//Resolve severity to a string.
	string severityString = getSeverityString(severity);

	string colorCode = "";
	if(myColorMode == LOG_COLORS_ON) {
		colorCode = getSeverityStringConsoleCode(severity);
	}

	//Concatenate and validate a format string prefix.
	string prefixFormat = "[" + timeString + "] " + colorCode + severityString + ": " + name + ": ";
	if(prefixFormat.find('%') != string::npos) {
		throw logic_error("Logger error, log line prefix is invalid!");
	}

	//Format string suffix.
	string suffixFormat = CONSOLE_COLOR_RESETALL "\n";

	//Assemble the final format string and send it to the output target.
	string finalFormat = prefixFormat + originalFormat + suffixFormat;
	vfprintf(myOutFile, finalFormat.c_str(), args);
}

void Logger::setLoggingTarget(std::string filePath) {
	YerFace_MutexLock(staticMutex);

	// If we previously opened a file as a logging target,
	// setting a new logging target will force the old file to be closed.
	// Otherwise this line will have no effect.
	setLoggingTarget(stderr);

	// Open the specified file.
	FILE *myFile;
	if((myFile = fopen(filePath.c_str(), "wb")) == NULL) {
		throw invalid_argument("setLoggingTarget() failed because we could not open the destination file for writing!");
	}

	// Set the new file as a logging target.
	setLoggingTarget(myFile);

	// Remember that we "own" this file, so it can be closed later.
	outFileOpened = true;

	YerFace_MutexUnlock(staticMutex);
}

void Logger::setLoggingTarget(FILE *file) {
	YerFace_MutexLock(staticMutex);

	// If we own the old file handle, we need to close it before losing track of it.
	if(outFileOpened) {
		fclose(outFile);
		outFileOpened = false;
	}

	// Set the new target.
	outFile = file;

	// Until we've tested the output device, we don't know if it's eligible for output colorization.
	colorsEligible = LOG_COLORS_CONSOLE_ELIGIBILITY_UNKNOWN;
	YerFace_MutexUnlock(staticMutex);
}

void Logger::setLoggingColorMode(LogColorModes mode) {
	YerFace_MutexLock(staticMutex);
	colorMode = mode;
	YerFace_MutexUnlock(staticMutex);
}

void Logger::setLoggingFilter(LogMessageSeverity severity) {
	YerFace_MutexLock(staticMutex);
	if(severity > LOG_SEVERITY_MAX) {
		severityFilter = LOG_SEVERITY_MAX;
	} else {
		severityFilter = severity;
	}
	YerFace_MutexUnlock(staticMutex);
}

std::string Logger::getSeverityString(LogMessageSeverity severity) {
	switch(severity) {
		case LOG_SEVERITY_EMERG:
			return "EMERGENCY";
		case LOG_SEVERITY_ALERT:
			return "ALERT";
		case LOG_SEVERITY_CRIT:
			return "CRITICAL";
		case LOG_SEVERITY_ERR:
			return "ERROR";
		case LOG_SEVERITY_WARNING:
			return "WARNING";
		case LOG_SEVERITY_NOTICE:
			return "NOTICE";
		case LOG_SEVERITY_INFO:
			return "INFO";
		case LOG_SEVERITY_DEBUG1:
			return "DEBUG1";
		case LOG_SEVERITY_DEBUG2:
			return "DEBUG2";
		case LOG_SEVERITY_DEBUG3:
			return "DEBUG3";
		case LOG_SEVERITY_DEBUG4:
			return "DEBUG4";
	}
	return "?????";
}

std::string Logger::getSeverityStringConsoleCode(LogMessageSeverity severity) {
	switch(severity) {
		case LOG_SEVERITY_EMERG:
			return CONSOLE_FONT_BOLD_ON CONSOLE_FONT_REVERSE_ON CONSOLE_COLOR_FOREGROUND_RED;
		case LOG_SEVERITY_ALERT:
			return CONSOLE_FONT_BOLD_ON CONSOLE_FONT_REVERSE_ON CONSOLE_COLOR_FOREGROUND_RED;
		case LOG_SEVERITY_CRIT:
			return CONSOLE_FONT_BOLD_ON CONSOLE_COLOR_FOREGROUND_RED;
		case LOG_SEVERITY_ERR:
			return CONSOLE_COLOR_FOREGROUND_RED;
		case LOG_SEVERITY_WARNING:
			return CONSOLE_COLOR_FOREGROUND_YELLOW;
		case LOG_SEVERITY_NOTICE:
			return CONSOLE_COLOR_FOREGROUND_BLUE;
		case LOG_SEVERITY_INFO:
			return CONSOLE_COLOR_FOREGROUND_CYAN;
		case LOG_SEVERITY_DEBUG1:
			return "";
		case LOG_SEVERITY_DEBUG2:
			return CONSOLE_COLOR_FOREGROUND_LIGHTGRAY;
		case LOG_SEVERITY_DEBUG3:
			return CONSOLE_FONT_DIM_ON;
		case LOG_SEVERITY_DEBUG4:
			return CONSOLE_FONT_DIM_ON CONSOLE_COLOR_FOREGROUND_LIGHTGRAY;
	}
	return "";
}

}; //namespace YerFace
