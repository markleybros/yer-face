
#include "Logger.hpp"
#include "Utilities.hpp"

#include <string>
#include <ctime>
#include <chrono>

#ifdef WIN32
#include <windows.h>
#include <wincon.h>
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#define my_localtime(NOW, TM) localtime_s(TM, NOW)
#define MILLIS_FORMAT "%03llu"
#else
#include <unistd.h>
#include <sys/time.h>
#define my_localtime(NOW, TM) localtime_r(NOW, TM)
#define MILLIS_FORMAT "%03lu"
#endif

using namespace std;

namespace YerFace {

#ifdef WIN32
#define CONSOLE_COLOR_FOREGROUND_RED		(FOREGROUND_RED | FOREGROUND_INTENSITY)
#define CONSOLE_COLOR_FOREGROUND_GREEN		(FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define CONSOLE_COLOR_FOREGROUND_YELLOW		(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define CONSOLE_COLOR_FOREGROUND_BLUE		(FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define CONSOLE_COLOR_FOREGROUND_MAGENTA	(FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define CONSOLE_COLOR_FOREGROUND_CYAN		(FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define CONSOLE_COLOR_FOREGROUND_WHITE		(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define CONSOLE_COLOR_FOREGROUND_LIGHTGRAY	(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define CONSOLE_COLOR_FOREGROUND_DARKGRAY	FOREGROUND_INTENSITY
#define CONSOLE_COLOR_FOREGROUND_BLACK		0

#define CONSOLE_COLOR_BACKGROUND_RED		(CONSOLE_COLOR_FOREGROUND_RED << 4)
#define CONSOLE_COLOR_BACKGROUND_GREEN		(CONSOLE_COLOR_FOREGROUND_GREEN << 4)
#define CONSOLE_COLOR_BACKGROUND_YELLOW		(CONSOLE_COLOR_FOREGROUND_YELLOW << 4)
#define CONSOLE_COLOR_BACKGROUND_BLUE		(CONSOLE_COLOR_FOREGROUND_BLUE << 4)
#define CONSOLE_COLOR_BACKGROUND_MAGENTA	(CONSOLE_COLOR_FOREGROUND_MAGENTA << 4)
#define CONSOLE_COLOR_BACKGROUND_CYAN		(CONSOLE_COLOR_FOREGROUND_CYAN << 4)
#define CONSOLE_COLOR_BACKGROUND_LIGHTGRAY	(CONSOLE_COLOR_FOREGROUND_LIGHTGRAY << 4)
#define CONSOLE_COLOR_BACKGROUND_DARKGRAY	(CONSOLE_COLOR_FOREGROUND_DARKGRAY << 4)
#define CONSOLE_COLOR_BACKGROUND_BLACK		(CONSOLE_COLOR_FOREGROUND_BLACK << 4)

#define CONSOLE_FONT_UNDERLINE				COMMON_LVB_UNDERSCORE
#define CONSOLE_FONT_REVERSE				COMMON_LVB_REVERSE_VIDEO
#else
#define CONSOLE_COLOR_RESETALL				"\x1B[0m"
#define CONSOLE_COLOR_FOREGROUND_RED		"\x1B[91m"
#define CONSOLE_COLOR_FOREGROUND_GREEN		"\x1B[92m"
#define CONSOLE_COLOR_FOREGROUND_YELLOW		"\x1B[93m"
#define CONSOLE_COLOR_FOREGROUND_BLUE		"\x1B[94m"
#define CONSOLE_COLOR_FOREGROUND_MAGENTA	"\x1B[95m"
#define CONSOLE_COLOR_FOREGROUND_CYAN		"\x1B[96m"
#define CONSOLE_COLOR_FOREGROUND_LIGHTGRAY	"\x1B[37m"
#define CONSOLE_COLOR_FOREGROUND_DARKGRAY	"\x1B[90m"
#define CONSOLE_COLOR_FOREGROUND_BLACK		"\x1B[30m"
#define CONSOLE_FONT_BOLD_ON				"\x1B[1m"
#define CONSOLE_FONT_DIM_ON					"\x1B[2m"
#define CONSOLE_FONT_UNDERLINE_ON			"\x1B[4m"
#define CONSOLE_FONT_REVERSE_ON				"\x1B[7m"
#define CONSOLE_FONT_BOLD_OFF				"\x1B[21m"
#define CONSOLE_FONT_DIM_OFF				"\x1B[22m"
#define CONSOLE_FONT_UNDERLINE_OFF			"\x1B[24m"
#define CONSOLE_FONT_REVERSE_OFF			"\x1B[27m"
#endif

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
	svlog(name, LOG_SEVERITY_DEBUG4, fmt, args);
	va_end(args);
}

void Logger::debug3(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	svlog(name, LOG_SEVERITY_DEBUG3, fmt, args);
	va_end(args);
}

void Logger::debug2(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	svlog(name, LOG_SEVERITY_DEBUG2, fmt, args);
	va_end(args);
}

void Logger::debug1(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	svlog(name, LOG_SEVERITY_DEBUG1, fmt, args);
	va_end(args);
}

void Logger::info(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	svlog(name, LOG_SEVERITY_INFO, fmt, args);
	va_end(args);
}

void Logger::notice(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	svlog(name, LOG_SEVERITY_NOTICE, fmt, args);
	va_end(args);
}

void Logger::warning(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	svlog(name, LOG_SEVERITY_WARNING, fmt, args);
	va_end(args);
}

void Logger::err(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	svlog(name, LOG_SEVERITY_ERR, fmt, args);
	va_end(args);
}

void Logger::crit(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	svlog(name, LOG_SEVERITY_CRIT, fmt, args);
	va_end(args);
}

void Logger::alert(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	svlog(name, LOG_SEVERITY_ALERT, fmt, args);
	va_end(args);
}

void Logger::emerg(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	svlog(name, LOG_SEVERITY_EMERG, fmt, args);
	va_end(args);
}

void Logger::log(LogMessageSeverity severity, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	svlog(name, severity, fmt, args);
	va_end(args);
}

void Logger::slog(std::string moduleName, LogMessageSeverity severity, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	svlog(moduleName, severity, fmt, args);
	va_end(args);
}

void Logger::svlog(std::string moduleName, LogMessageSeverity severity, const char *fmt, va_list args) {
	if(moduleName.find('%') != string::npos) {
		throw invalid_argument("Logger moduleName must not contain a percent sign.");
	}
	#ifdef WIN32
	HANDLE myWinConsole = NULL;
	#endif
	YerFace_MutexLock_Trivial(staticMutex);
	LogMessageSeverity mySeverityFilter = severityFilter;
	FILE *myOutFile = outFile;
	LogColorModes myColorMode = colorMode;
	if(myColorMode == LOG_COLORS_AUTO) {
		if(colorsEligible == LOG_COLORS_CONSOLE_ELIGIBILITY_UNKNOWN) {
			if(isatty(fileno(myOutFile))) {
				#ifdef WIN32
				colorsEligible = LOG_COLORS_CONSOLE_INELIGIBLE;
				if(outFile == stderr) {
					myWinConsole = GetStdHandle(STD_ERROR_HANDLE);
					if(myWinConsole != NULL && myWinConsole != INVALID_HANDLE_VALUE) {
						colorsEligible = LOG_COLORS_CONSOLE_ELIGIBLE;
					}
				}
				#else
				colorsEligible = LOG_COLORS_CONSOLE_ELIGIBLE;
				#endif
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
	YerFace_MutexUnlock_Trivial(staticMutex);

	//Drop messages according to the logging filter.
	if(severity > mySeverityFilter) {
		return;
	}

	//Trim any leading or trailing whitespace from fmt.
	string originalFormat = Utilities::stringTrim((string)fmt);

	//Resolve a time string. (Semi-portable.)
	uint64_t now_milli = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	uint64_t now_sec = now_milli / 1000;
	time_t now_time_t = (time_t)now_sec;
	uint64_t millis = now_milli % 1000;
	struct tm myTm;
	my_localtime(&now_time_t, &myTm); // This is a platform-dependent macro, see above.
	char timeStringIntermediate[64];
	strftime(timeStringIntermediate, sizeof(timeStringIntermediate), "%F %H:%M:%S", &myTm);
	char timeStringC[128];
	snprintf(timeStringC, sizeof(timeStringC), "%s." MILLIS_FORMAT, timeStringIntermediate, millis);
	std::string timeString = (string)timeStringC;

	//Resolve severity to a string.
	string severityString = getSeverityString(severity);

	// Dump the line -- platform dependent
	#ifdef WIN32

	//Concatenate and validate a format string prefix.
	string prefixFormat = "[" + timeString + "] " + severityString + ": " + moduleName + ": ";
	if(prefixFormat.find('%') != string::npos) {
		throw logic_error("Logger error, log line prefix is invalid!");
	}

	//Assemble the final format string and send it to the output target.
	string finalFormat = prefixFormat + originalFormat;

	YerFace_MutexLock_Trivial(staticMutex);
	if(myColorMode == LOG_COLORS_ON) {
		myWinConsole = GetStdHandle(STD_ERROR_HANDLE);
		SetConsoleTextAttribute(myWinConsole, getSeverityStringConsoleCode(severity));
	}
	vfprintf(myOutFile, finalFormat.c_str(), args);
	if(myColorMode == LOG_COLORS_ON) {
		SetConsoleTextAttribute(myWinConsole, CONSOLE_COLOR_FOREGROUND_WHITE | CONSOLE_COLOR_BACKGROUND_BLACK);
	}
	fprintf(myOutFile, "\n");
	YerFace_MutexUnlock_Trivial(staticMutex);

	#else // End WIN32, Begin Non-WIN32

	string colorCode = "";
	if(myColorMode == LOG_COLORS_ON) {
		colorCode = getSeverityStringConsoleCode(severity);
	}

	//Concatenate and validate a format string prefix.
	string prefixFormat = "[" + timeString + "] " + colorCode + severityString + ": " + moduleName + ": ";
	if(prefixFormat.find('%') != string::npos) {
		throw logic_error("Logger error, log line prefix is invalid!");
	}

	//Format string suffix.
	string suffixFormat = CONSOLE_COLOR_RESETALL "\n";

	//Assemble the final format string and send it to the output target.
	string finalFormat = prefixFormat + originalFormat + suffixFormat;
	YerFace_MutexLock_Trivial(staticMutex);
	vfprintf(myOutFile, finalFormat.c_str(), args);
	YerFace_MutexUnlock_Trivial(staticMutex);

	#endif // End Non-WIN32
}

void Logger::setLoggingTarget(std::string filePath) {
	YerFace_MutexLock_Trivial(staticMutex);

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

	YerFace_MutexUnlock_Trivial(staticMutex);
}

void Logger::setLoggingTarget(FILE *file) {
	YerFace_MutexLock_Trivial(staticMutex);

	// If we own the old file handle, we need to close it before losing track of it.
	if(outFileOpened) {
		fclose(outFile);
		outFileOpened = false;
	}

	// Set the new target.
	outFile = file;

	// Until we've tested the output device, we don't know if it's eligible for output colorization.
	colorsEligible = LOG_COLORS_CONSOLE_ELIGIBILITY_UNKNOWN;
	YerFace_MutexUnlock_Trivial(staticMutex);
}

void Logger::setLoggingColorMode(LogColorModes mode) {
	YerFace_MutexLock_Trivial(staticMutex);
	colorMode = mode;
	YerFace_MutexUnlock_Trivial(staticMutex);
}

void Logger::setLoggingFilter(LogMessageSeverity severity) {
	YerFace_MutexLock_Trivial(staticMutex);
	if(severity > LOG_SEVERITY_MAX) {
		severityFilter = LOG_SEVERITY_MAX;
	} else {
		severityFilter = severity;
	}
	YerFace_MutexUnlock_Trivial(staticMutex);
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

LogConsoleCode Logger::getSeverityStringConsoleCode(LogMessageSeverity severity) {
	LogConsoleCode ourCode;
	#ifdef WIN32
	ourCode = CONSOLE_COLOR_FOREGROUND_WHITE | CONSOLE_COLOR_BACKGROUND_BLACK;
	#else
	ourCode = "";
	#endif

	switch(severity) {
		case LOG_SEVERITY_EMERG:
			#ifdef WIN32
			ourCode = CONSOLE_COLOR_FOREGROUND_RED | CONSOLE_FONT_UNDERLINE | CONSOLE_FONT_REVERSE;
			#else
			ourCode = CONSOLE_FONT_BOLD_ON CONSOLE_FONT_REVERSE_ON CONSOLE_COLOR_FOREGROUND_RED;
			#endif
			break;
		case LOG_SEVERITY_ALERT:
			#ifdef WIN32
			ourCode = CONSOLE_COLOR_FOREGROUND_RED | CONSOLE_FONT_UNDERLINE | CONSOLE_FONT_REVERSE;
			#else
			ourCode = CONSOLE_FONT_BOLD_ON CONSOLE_FONT_REVERSE_ON CONSOLE_COLOR_FOREGROUND_RED;
			#endif
			break;
		case LOG_SEVERITY_CRIT:
			#ifdef WIN32
			ourCode = CONSOLE_COLOR_FOREGROUND_RED | CONSOLE_FONT_UNDERLINE;
			#else
			ourCode = CONSOLE_FONT_BOLD_ON CONSOLE_COLOR_FOREGROUND_RED;
			#endif
			break;
		case LOG_SEVERITY_ERR:
			ourCode = CONSOLE_COLOR_FOREGROUND_RED;
			break;
		case LOG_SEVERITY_WARNING:
			ourCode = CONSOLE_COLOR_FOREGROUND_YELLOW;
			break;
		case LOG_SEVERITY_NOTICE:
			ourCode = CONSOLE_COLOR_FOREGROUND_BLUE;
			break;
		case LOG_SEVERITY_INFO:
			ourCode = CONSOLE_COLOR_FOREGROUND_CYAN;
			break;
		case LOG_SEVERITY_DEBUG1:
			#ifdef WIN32
			ourCode = CONSOLE_COLOR_FOREGROUND_WHITE | FOREGROUND_INTENSITY;
			#else
			//// Default
			#endif
			break;
		case LOG_SEVERITY_DEBUG2:
			#ifdef WIN32
			ourCode = CONSOLE_COLOR_FOREGROUND_WHITE;
			#else
			ourCode = CONSOLE_COLOR_FOREGROUND_LIGHTGRAY;
			#endif
			break;
		case LOG_SEVERITY_DEBUG3:
			#ifdef WIN32
			ourCode = CONSOLE_COLOR_FOREGROUND_DARKGRAY;
			#else
			ourCode = CONSOLE_FONT_DIM_ON;
			#endif
			break;
		case LOG_SEVERITY_DEBUG4:
			#ifdef WIN32
			ourCode = CONSOLE_COLOR_FOREGROUND_DARKGRAY;
			#else
			ourCode = CONSOLE_FONT_DIM_ON CONSOLE_COLOR_FOREGROUND_LIGHTGRAY;
			#endif
			break;
	}

	return ourCode;
}

}; //namespace YerFace
