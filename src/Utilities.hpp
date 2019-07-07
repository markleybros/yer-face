#pragma once

#include "Logger.hpp"

#include "SDL.h"
#include "opencv2/imgproc.hpp"
#include "json.hpp"

#include <exception>
#include <vector>
#include <fstream>
#include <chrono>

using namespace std;
using json = nlohmann::json;

namespace YerFace {

#ifdef WIN32
#define YERFACE_PATH_SEP "\\"
#else
#define YERFACE_PATH_SEP "/"
#endif

using cstr = const char * const;

static constexpr cstr portableBasename(cstr str, cstr lastSep) {
	return
		// If str is pointing to the end of the string, return lastSep
		*str == '\0' ? lastSep :
			// Otherwise, if str is pointing to a valid path separator, remember and recurse.
			(*str == '/' || *str =='\\') ? portableBasename(str + 1, str + 1) :
				// Otherwise, leave the last known valid separator alone and recurse.
				portableBasename(str + 1, lastSep);
}

static constexpr cstr portableBasename(cstr str) {
	return portableBasename(str, str);
}

#ifdef WIN32
#define YERFACE_FILE portableBasename(__FILE__)
#else
#define YERFACE_FILE ({constexpr cstr sf__ {portableBasename(__FILE__)}; sf__;})
#endif

#define YerFace_MutexLock_Trivial(X) do {							\
	if(SDL_LockMutex(X) != 0) {										\
		throw runtime_error("Failed to lock mutex trivially.");		\
	}																\
} while(0)

#define YerFace_MutexUnlock_Trivial(X) do {							\
	if(SDL_UnlockMutex(X) != 0) {									\
		throw runtime_error("Failed to unlock mutex trivially.");	\
	}																\
} while(0)

//// YERFACE_MUTEX_TRIVIAL replaces all mutex operations with their trivial counterparts.
// #define YERFACE_MUTEX_TRIVIAL

#ifdef YERFACE_MUTEX_TRIVIAL

#define YerFace_MutexLock YerFace_MutexLock_Trivial
#define YerFace_MutexUnlock YerFace_MutexUnlock_Trivial

#else // End Trivial mutex macros, begin regular mutex macros

#define YerFace_MutexLock(X) do {														\
	int _mutexStatus = SDL_MUTEX_TIMEDOUT;												\
	uint64_t _mutexStart = std::chrono::duration_cast<std::chrono::milliseconds>		\
		(std::chrono::steady_clock::now().time_since_epoch()).count();					\
	YerFace_SLog("Utilities", LOG_SEVERITY_DEBUG4, "Attempting lock on mutex "			\
		"%s (%p) ...", #X, X);															\
	while(_mutexStatus == SDL_MUTEX_TIMEDOUT) {											\
		_mutexStatus = SDL_TryLockMutex(X);												\
		if(_mutexStatus == -1) {														\
			YerFace_SLog("Utilities", LOG_SEVERITY_CRIT, "Failed to lock mutex " 		\
				"%s (%p). Error was: %s", #X, X, SDL_GetError());						\
			throw runtime_error("Failed to lock mutex.");								\
		} else if(_mutexStatus == SDL_MUTEX_TIMEDOUT) {									\
			uint64_t _mutexEnd = std::chrono::duration_cast<std::chrono::milliseconds>	\
				(std::chrono::steady_clock::now().time_since_epoch()).count();			\
			YerFace_SLog("Utilities", LOG_SEVERITY_DEBUG4, "Lock attempt on mutex "		\
				"%s (%p) timed out...", #X, X);											\
			if(_mutexEnd - _mutexStart > 4000) {										\
				YerFace_SLog("Utilities", LOG_SEVERITY_CRIT, "Lock attempt on mutex "	\
					"%s (%p) timed out! No more retries...", #X, X);					\
				throw runtime_error("Break glass! Mutex lock timed out; possible "		\
					"deadlock. (This was probably caused by an earlier exception!!!)");	\
			}																			\
		}																				\
	}																					\
	YerFace_SLog("Utilities", LOG_SEVERITY_DEBUG4, "Successfully locked mutex "			\
		"%s (%p) ...", #X, X);	\
} while(0)

#define YerFace_MutexUnlock(X) do {														\
	if(SDL_UnlockMutex(X) != 0) {														\
		YerFace_SLog("Utilities", LOG_SEVERITY_CRIT, "Failed to unlock mutex "			\
			"%s (%p). Error was: %s", #X, X, SDL_GetError());							\
		throw runtime_error("Failed to unlock mutex.");									\
	}																					\
	YerFace_SLog("Utilities", LOG_SEVERITY_DEBUG4, "Successfully unlocked mutex "		\
		"%s (%p) ...", #X, X);															\
} while(0)

#endif // End regular mutex macros

#define YerFace_CarefullyDelete(logger, status, x) do {					\
	try {																\
		delete x;														\
	} catch(exception &e) {												\
		logger->emerg("%s Destructor exception: %s\n", #x, e.what());	\
		status->setEmergency();											\
	}																	\
} while(0)

#define YerFace_CarefullyDelete_NoStatus(logger, x) do {				\
	try {																\
		delete x;														\
	} catch(exception &e) {												\
		logger->emerg("%s Destructor exception: %s\n", #x, e.what());	\
	}																	\
} while(0)

class TimeIntervalComparison {
public:
	bool doesAEndBeforeB;
	bool doesAOccurBeforeB;
	bool doesAOccurDuringB;
	bool doesAOccurAfterB;
	bool doesAStartAfterB;
};

class Logger;

class Utilities {
public:
	static double normalize(double x, double length);
	static cv::Rect2d scaleRect(cv::Rect2d rect, double scale);
	static cv::Rect2d insetBox(cv::Rect2d originalBox, double scale);
	static cv::Point2d centerRect(cv::Rect2d rect);
	static cv::Point2d averagePoint(std::vector<cv::Point2d> points);
	static double lineDistance(cv::Point2d a, cv::Point2d b);
	static double lineDistance(cv::Point3d a, cv::Point3d b);
	static TimeIntervalComparison timeIntervalCompare(double startTimeA, double endTimeA, double startTimeB, double endTimeB);
	static double degreesToRadians(double degrees);
	static double radiansToDegrees(double radians, bool normalize = true);
	static cv::Vec3d rotationMatrixToEulerAngles(cv::Mat &R, bool returnDegrees = true, bool degreesReflectAroundZero = true);
	static cv::Mat eulerAnglesToRotationMatrix(cv::Vec3d &R, bool expectDegrees = true);
	static double degreesDifferenceBetweenTwoRotationMatrices(cv::Mat a, cv::Mat b);
	static cv::Mat generateFakeCameraMatrix(double focalLength = 1.0, cv::Point2d principalPoint = cv::Point2d(0, 0));
	static bool rayPlaneIntersection(cv::Point3d &intersection, cv::Point3d rayOrigin, cv::Vec3d rayVector, cv::Point3d planePoint, cv::Vec3d planeNormal);
	static void drawRotatedRectOutline(cv::Mat frame, cv::RotatedRect rrect, cv::Scalar color = cv::Scalar(0, 0, 255), int thickness = 1);
	static void drawX(cv::Mat frame, cv::Point2d markerPoint, cv::Scalar color = cv::Scalar(0, 0, 255), int lineLength = 5, int thickness = 1);
	static cv::Scalar scalarColorFromJSONArray(string jsonArrayString);
	static cv::Scalar scalarColorFromJSONArray(json jsonArray);
	static json JSONArrayFromScalarColor(cv::Scalar color);
	static cv::Point3d Point3dFromJSONArray(json jsonArray);
	static bool stringEndMatches(string haystack, string needle);
	static bool fileExists(string filePath);
	static string fileSearchInCommonLocations(string filePath);
	static string fileValidPathOrDie(string filePath, bool searchOnly = false);
	static string stringTrim(std::string str);
	static string stringTrimLeft(std::string str);
	static string stringTrimRight(std::string str);

private:
	static Logger *logger;
	static char *sdlDataPath;
};

typedef intmax_t FrameNumber;
#define YERFACE_FRAMENUMBER_FORMATINNER PRIdMAX
#define YERFACE_FRAMENUMBER_FORMAT "%" YERFACE_FRAMENUMBER_FORMATINNER

class FrameTimestamps {
public:
	double startTimestamp;
	double estimatedEndTimestamp;
	FrameNumber frameNumber;
};

}; //namespace YerFace
