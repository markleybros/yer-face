#pragma once

#include "Logger.hpp"

#include "SDL.h"
#include "opencv2/imgproc.hpp"
#include "json.hpp"

#include <exception>
#include <vector>
#include <fstream>

using namespace std;
using json = nlohmann::json;

namespace YerFace {

//#define YERFACE_MUTEX_DEBUG

#ifdef YERFACE_MUTEX_DEBUG

#include "pthread.h"

#define YerFace_MutexLock(X) do {						\
	fprintf(stderr, "%s:%d: THREAD %lu ATTEMPTING MUTEX LOCK %s (0x%lX)\n", __FILE__, __LINE__, pthread_self(), (#X), (long unsigned int)(X));	\
	if(SDL_LockMutex(X) != 0) {							\
		throw runtime_error("Failed to lock mutex.");	\
	}													\
	fprintf(stderr, "%s:%d: THREAD %lu SUCCESSFULLY LOCKED MUTEX %s (0x%lX)\n", __FILE__, __LINE__, pthread_self(), (#X), (long unsigned int)(X));	\
} while(0)

#define YerFace_MutexUnlock(X) do {						\
	fprintf(stderr, "%s:%d: THREAD %lu UNLOCKING MUTEX %s (0x%lX)\n", __FILE__, __LINE__, pthread_self(), (#X), (long unsigned int)(X));	\
	if(SDL_UnlockMutex(X) != 0) {						\
		throw runtime_error("Failed to unlock mutex.");	\
	}													\
} while(0)

#else // End Mutex Debugging Macros, Begin Regular Mutex Macros

#define YerFace_MutexLock(X) do {														\
	int mutexStatus = SDL_MUTEX_TIMEDOUT;												\
	Uint32 mutexStartLock = SDL_GetTicks();												\
	while(mutexStatus == SDL_MUTEX_TIMEDOUT) {											\
		mutexStatus = SDL_TryLockMutex(X);												\
		if(mutexStatus == -1) {															\
			throw runtime_error("Failed to lock mutex.");								\
		} else if(mutexStatus == SDL_MUTEX_TIMEDOUT) {									\
			if(SDL_GetTicks() - mutexStartLock > 4000) {								\
				throw runtime_error("Break glass! Mutex lock timed out; possible "		\
					"deadlock. (This was probably caused by an earlier exception!!!)");	\
			}																			\
		}																				\
	}																					\
} while(0)

#define YerFace_MutexUnlock(X) do {						\
	if(SDL_UnlockMutex(X) != 0) {						\
		throw runtime_error("Failed to unlock mutex.");	\
	}													\
} while(0)

#endif // End Regular Mutex Macros

#ifdef WIN32
#define YERFACE_PATH_SEP "\\"
#else
#define YERFACE_PATH_SEP "/"
#endif

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
