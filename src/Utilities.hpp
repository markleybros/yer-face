#pragma once

#include "SDL.h"
#include "opencv2/imgproc.hpp"
#include "json.hpp"

#include <exception>

using namespace std;
using namespace cv;
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

class Utilities {
public:
	static double normalize(double x, double length);
	static Rect2d scaleRect(Rect2d rect, double scale);
	static Rect2d insetBox(Rect2d originalBox, double scale);
	static Point2d centerRect(Rect2d rect);
	static Point2d averagePoint(vector<Point2d> points);
	static double lineDistance(Point2d a, Point2d b);
	static double lineDistance(Point3d a, Point3d b);
	static double degreesToRadians(double degrees);
	static double radiansToDegrees(double radians, bool normalize = true);
	static Vec3d rotationMatrixToEulerAngles(Mat &R, bool returnDegrees = true, bool degreesReflectAroundZero = true);
	static Mat eulerAnglesToRotationMatrix(Vec3d &R, bool expectDegrees = true);
	static double degreesDifferenceBetweenTwoRotationMatrices(Mat a, Mat b);
	static Mat generateFakeCameraMatrix(double focalLength = 1.0, Point2d principalPoint = Point2d(0, 0));
	static bool rayPlaneIntersection(Point3d &intersection, Point3d rayOrigin, Vec3d rayVector, Point3d planePoint, Vec3d planeNormal);
	static void drawRotatedRectOutline(Mat frame, RotatedRect rrect, Scalar color = Scalar(0, 0, 255), int thickness = 1);
	static void drawX(Mat frame, Point2d markerPoint, Scalar color = Scalar(0, 0, 255), int lineLength = 5, int thickness = 1);
	static cv::Scalar scalarColorFromJSONArray(String jsonArrayString);
	static cv::Scalar scalarColorFromJSONArray(json jsonArray);
	static json JSONArrayFromScalarColor(cv::Scalar color);
	static cv::Point3d Point3dFromJSONArray(json jsonArray);
};

}; //namespace YerFace
