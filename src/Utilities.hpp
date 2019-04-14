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

#else

#define YerFace_MutexLock(X) do { if(SDL_LockMutex(X) != 0) { throw runtime_error("Failed to lock mutex."); } } while(0)
#define YerFace_MutexUnlock(X) do { if(SDL_UnlockMutex(X) != 0) { throw runtime_error("Failed to unlock mutex."); } } while(0)

#endif

#ifdef WIN32
#define YERFACE_PATH_SEP "\\"
#else
#define YERFACE_PATH_SEP "/"
#endif

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
